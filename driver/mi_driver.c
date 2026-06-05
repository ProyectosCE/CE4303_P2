#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/netdevice.h>
#include <linux/skbuff.h>
#include <linux/fs.h>      /**< Infraestructura de dispositivos de caracteres (registro en /dev). */
#include <linux/uaccess.h> /**< Helpers de acceso a memoria de usuario (p.ej., copy_from_user). */
#include <linux/slab.h>    /**< Asignación de memoria en kernel (kmalloc/kfree). */

/**
 * @file mi_driver.c
 * @brief Driver de kernel para transferencia de datos (imagen) hacia una interfaz de red USB.
 *
 * @details
 * Este módulo registra un dispositivo de caracteres y utiliza su primitiva de escritura
 * para tomar un buffer en espacio de usuario, fragmentarlo en ráfagas de tamaño acotado
 * y transmitir cada fragmento como un paquete inyectado en la pila de red.
 *
 * El driver también instala un hook de captura (packet_type) para recibir tráfico de la
 * interfaz objetivo y, potencialmente, procesar acuses/confirmaciones (actualmente es un
 * stub que descarta los paquetes interceptados).
 *
 * @note
 * La interfaz de red se selecciona dinámicamente buscando un nombre que inicie con "enx".
 * Este patrón suele corresponder a interfaces USB-Ethernet en Linux.
 */

/**
 * @def DEVICE_NAME
 * @brief Nombre del dispositivo de caracteres expuesto en /dev/.
 */
#define DEVICE_NAME "raspberry_usb"

/** @brief Major dinámico asignado al registrar el dispositivo de caracteres. */
static int major_number;

/**
 * @brief Hook de captura a nivel de enlace para interceptar tráfico entrante.
 *
 * @details
 * Se registra con dev_add_pack() para capturar tráfico de tipo ETH_P_ALL.
 */
static struct packet_type mi_packet_hook;

/**
 * @brief Primitiva write(2) del dispositivo de caracteres.
 *
 * @details
 * Implementa un esquema de transferencia tipo stream desde espacio de usuario hacia la
 * interfaz de red seleccionada. Para controlar la presión de memoria/colas, fragmenta el
 * payload en bloques de tamaño fijo y aplica un retardo entre fragmentos.
 *
 * Al finalizar, transmite un marcador ASCII ("FIN_TRANSMISION") para permitir al receptor
 * delimitar el final del flujo.
 *
 * @param file   Descriptor de archivo del dispositivo.
 * @param buffer Buffer en espacio de usuario con los datos a transmitir.
 * @param len    Longitud total solicitada.
 * @param offset Offset asociado al archivo (no utilizado; se ignora).
 *
 * @return En éxito, retorna @p len. En fallo, retorna un errno negativo.
 */
static ssize_t driver_write(struct file *file, const char __user *buffer, size_t len, loff_t *offset) {
    char *kernel_buffer;        /**< Buffer temporal para el fragmento en kernel. */
    struct sk_buff *skb;        /**< Contenedor de paquete (sk_buff) a inyectar. */
    struct net_device *dev;     /**< Interfaz seleccionada (prefijo "enx"). */
    struct net_device *net_dev; /**< Iterador de interfaces durante el escaneo. */
    unsigned char *data;        /**< Puntero al payload dentro del sk_buff. */
    size_t bytes_enviados = 0;  /**< Contador de bytes ya transmitidos desde @p buffer. */
    size_t tamano_bloque = 1024; /**< Tamaño máximo de ráfaga: limita presión en el receptor. */

    printk(KERN_INFO "[Mi_Driver_Proyecto] Procesando archivo del usuario. Tamaño total solicitado: %zu bytes\n", len);

    /**
     * @brief Resolver la interfaz objetivo por prefijo de nombre.
     *
     * Se protege el recorrido de dispositivos con el lock de lectura y se toma una
     * referencia con dev_hold() para evitar que el net_device desaparezca.
     */
    dev = NULL;
    read_lock(&dev_base_lock);
    for_each_netdev(&init_net, net_dev) {
        if (strncmp(net_dev->name, "enx", 3) == 0) {
            dev = net_dev;
            dev_hold(dev); /**< Protege contra liberación concurrente del net_device. */
            break;
        }
    }
    read_unlock(&dev_base_lock);

    if (!dev) {
        printk(KERN_ERR "[Mi_Driver_Proyecto] Error: Interfaz física de la Pi no encontrada.\n");
        return -ENODEV;
    }

    /**
     * @brief Fragmentar y transmitir en bucle.
     *
     * Cada fragmento se copia desde usuario, se empaqueta en un sk_buff y se inyecta
     * hacia transmisión con dev_queue_xmit().
     */
    while (bytes_enviados < len) {
        size_t raga_actual = len - bytes_enviados;
        if (raga_actual > tamano_bloque) {
            raga_actual = tamano_bloque;
        }

        kernel_buffer = kmalloc(raga_actual, GFP_KERNEL); /**< Buffer temporal en heap de kernel. */
        if (!kernel_buffer) {
            dev_put(dev);
            return -ENOMEM;
        }

        if (copy_from_user(kernel_buffer, buffer + bytes_enviados, raga_actual)) {
            kfree(kernel_buffer);
            dev_put(dev);
            return -EFAULT;
        }

        /**
         * @brief Crear el sk_buff (socket buffer) para el fragmento actual.
         *
         * Se añade headroom (LL_RESERVED_SPACE) para que la capa de enlace pueda
         * anteponer headers según el dispositivo.
         */
        skb = dev_alloc_skb(raga_actual + LL_RESERVED_SPACE(dev));
        if (!skb) {
            kfree(kernel_buffer);
            dev_put(dev);
            return -ENOMEM;
        }

        skb_reserve(skb, LL_RESERVED_SPACE(dev)); /**< Reserva headroom para encabezados L2. */
        skb->dev = dev;
        skb->protocol = htons(ETH_P_ALL); /**< Protocolo genérico (sin demux específico). */

        data = skb_put(skb, raga_actual); /** Reservar tailroom para payload y obtener puntero a datos. */
        memcpy(data, kernel_buffer, raga_actual);

        /**
         * @brief Encolar el fragmento para transmisión.
         *
         * Tras dev_queue_xmit(), el skb pasa a ser propiedad de la pila TX.
         */
        dev_queue_xmit(skb);

        kfree(kernel_buffer);
        bytes_enviados += raga_actual;
        
        /**
         * @brief Pequeño retardo (throttling) entre fragmentos.
         *
         * Retardo fijo de 100 microsegundos para dar oportunidad al receptor de procesar
         * y vaciar su cola/búfer.
         */
        udelay(100);
    }

    printk(KERN_INFO "[Mi_Driver_Proyecto] Transmisión completada con éxito. %zu bytes enviados en fragmentos de 1KB.\n", bytes_enviados);
    /** @brief Inyectar marcador de fin de transmisión para el receptor. */
    char *eof_msg = "FIN_TRANSMISION";
    size_t eof_len = strlen(eof_msg);
    
    kernel_buffer = kmalloc(eof_len, GFP_KERNEL);
    if (kernel_buffer) {
        memcpy(kernel_buffer, eof_msg, eof_len);
        skb = dev_alloc_skb(eof_len + LL_RESERVED_SPACE(dev));
        if (skb) {
            skb_reserve(skb, LL_RESERVED_SPACE(dev));
            skb->dev = dev;
            skb->protocol = htons(ETH_P_ALL);
            data = skb_put(skb, eof_len);
            memcpy(data, kernel_buffer, eof_len);
            dev_queue_xmit(skb); /**< Encolar el paquete EOF. */
        }
        kfree(kernel_buffer);
    }
    dev_put(dev); /**< Liberar referencia tomada con dev_hold(). */
    return len;
}


/**
 * @brief Tabla de operaciones de archivo expuestas por el dispositivo de caracteres.
 */
static struct file_operations fops = {
    .write = driver_write, /**< Implementación de write(2) del dispositivo. */
};

/**
 * @brief Callback de recepción de paquetes para el hook de red.
 *
 * @details
 * Este callback se invoca cuando el kernel entrega un paquete al packet_type
 * registrado por el módulo. Actualmente no interpreta el contenido: solo verifica
 * la interfaz por prefijo "enx" y descarta el paquete (kfree_skb).
 *
 * @param skb      Paquete recibido.
 * @param dev      Dispositivo de red que recibió el paquete.
 * @param pt       Packet type que originó la entrega.
 * @param orig_dev Dispositivo original (si aplica en escenarios con bridge).
 *
 * @return NET_RX_SUCCESS.
 */
static int mi_packet_rcv(struct sk_buff *skb, struct net_device *dev, struct packet_type *pt, struct net_device *orig_dev) {
    if (strncmp(dev->name, "enx", 3) == 0) {
        /** @brief Punto de extensión: procesar acuses/confirmaciones del receptor. */
    }
    kfree_skb(skb); /**< Consumir el skb para evitar fugas (este hook lo descarta). */
    return NET_RX_SUCCESS;
}

/**
 * @brief Punto de entrada del módulo.
 *
 * @details
 * - Registra el dispositivo de caracteres con major dinámico.
 * - Configura y registra un hook de paquetes para ETH_P_ALL.
 *
 * @return 0 en éxito; en error, un errno negativo.
 */
static int __init mi_driver_init(void) {
    printk(KERN_INFO "[Mi_Driver_Proyecto] Inicializando sistema de transferencia de imágenes...\n");
    
    /** @brief Registrar el dispositivo de caracteres en /dev/ con major dinámico. */
    major_number = register_chrdev(0, DEVICE_NAME, &fops);
    if (major_number < 0) {
        printk(KERN_ERR "[Mi_Driver_Proyecto] Falló el registro del dispositivo de caracteres.\n");
        return major_number;
    }
    printk(KERN_INFO "[Mi_Driver_Proyecto] Dispositivo creado. Ejecutar: 'sudo mknod /dev/%s c %d 0' para activarlo.\n", DEVICE_NAME, major_number);
    
    /** @brief Registrar el hook de red para captura de paquetes. */
    mi_packet_hook.type = htons(ETH_P_ALL);
    mi_packet_hook.func = mi_packet_rcv;
    dev_add_pack(&mi_packet_hook);
    
    return 0;
}

/**
 * @brief Punto de salida del módulo.
 *
 * @details
 * Desregistra el hook de paquetes y el dispositivo de caracteres.
 */
static void __exit mi_driver_exit(void) {
    dev_remove_pack(&mi_packet_hook);
    unregister_chrdev(major_number, DEVICE_NAME);
    printk(KERN_INFO "[Mi_Driver_Proyecto] Driver de imágenes descargado del sistema.\n");
}

/** @brief Registrar la rutina de inicialización del módulo. */
module_init(mi_driver_init);
/** @brief Registrar la rutina de salida del módulo. */
module_exit(mi_driver_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("PB Team");
MODULE_DESCRIPTION("Driver de Kernel a la medida para transferencia de imágenes por OTG");
