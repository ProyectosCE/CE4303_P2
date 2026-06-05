#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/netdevice.h>
#include <linux/skbuff.h>
#include <linux/fs.h>      // Necesario para registrar dispositivos en /dev/
#include <linux/uaccess.h> // Necesario para pasar datos de Usuario a Kernel (copy_from_user)
#include <linux/slab.h>    // Necesario para kmalloc y kfree

#define DEVICE_NAME "raspberry_usb"

static int major_number;
static struct packet_type mi_packet_hook;

// Esta función se ejecuta cuando el usuario envía la imagen al archivo /dev/raspberry_usb
static ssize_t driver_write(struct file *file, const char __user *buffer, size_t len, loff_t *offset) {
    char *kernel_buffer;
    struct sk_buff *skb;
    struct net_device *dev;
    struct net_device *net_dev;
    unsigned char *data;
    size_t bytes_enviados = 0;
    size_t tamano_bloque = 1024; // Limitamos cada ráfaga a 1 KB para proteger el búfer de la Pi

    printk(KERN_INFO "[Mi_Driver_Proyecto] Procesando archivo del usuario. Tamaño total solicitado: %zu bytes\n", len);

    // 1. Buscar dinámicamente la interfaz de la Pi (enx*)
    dev = NULL;
    read_lock(&dev_base_lock);
    for_each_netdev(&init_net, net_dev) {
        if (strncmp(net_dev->name, "enx", 3) == 0) {
            dev = net_dev;
            dev_hold(dev);
            break;
        }
    }
    read_unlock(&dev_base_lock);

    if (!dev) {
        printk(KERN_ERR "[Mi_Driver_Proyecto] Error: Interfaz física de la Pi no encontrada.\n");
        return -ENODEV;
    }

    // 2. Fragmentar y transmitir en bucle de 1 KB
    while (bytes_enviados < len) {
        size_t raga_actual = len - bytes_enviados;
        if (raga_actual > tamano_bloque) {
            raga_actual = tamano_bloque;
        }

        kernel_buffer = kmalloc(raga_actual, GFP_KERNEL);
        if (!kernel_buffer) {
            dev_put(dev);
            return -ENOMEM;
        }

        if (copy_from_user(kernel_buffer, buffer + bytes_enviados, raga_actual)) {
            kfree(kernel_buffer);
            dev_put(dev);
            return -EFAULT;
        }

        // Crear el Socket Buffer específico para este fragmento
        skb = dev_alloc_skb(raga_actual + LL_RESERVED_SPACE(dev));
        if (!skb) {
            kfree(kernel_buffer);
            dev_put(dev);
            return -ENOMEM;
        }

        skb_reserve(skb, LL_RESERVED_SPACE(dev));
        skb->dev = dev;
        skb->protocol = htons(ETH_P_ALL);

        data = skb_put(skb, raga_actual);
        memcpy(data, kernel_buffer, raga_actual);

        // Inyectar el fragmento seguro al cable
        dev_queue_xmit(skb);

        kfree(kernel_buffer);
        bytes_enviados += raga_actual;
        
        // Un retraso diminuto de 100 microsegundos en el Kernel Space 
        // para darle tiempo al procesador de la Pi de respirar y vaciar su búfer
        udelay(100); 
    }

    printk(KERN_INFO "[Mi_Driver_Proyecto] Transmisión completada con éxito. %zu bytes enviados en fragmentos de 1KB.\n", bytes_enviados);
    // INYECCIÓN DEL MARCADOR DE FIN DE ARCHIVO
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
            dev_queue_xmit(skb);
        }
        kfree(kernel_buffer);
    }
    dev_put(dev);
    return len;
}


// Estructura de operaciones de archivo (primitiva)
static struct file_operations fops = {
    .write = driver_write, // Vincular la primitiva de escritura
};

// Interceptor de respuestas de la Pi (Por si la Pi avisa que recibió la imagen con éxito)
static int mi_packet_rcv(struct sk_buff *skb, struct net_device *dev, struct packet_type *pt, struct net_device *orig_dev) {
    if (strncmp(dev->name, "enx", 3) == 0) {
        // Interceptar confirmaciones de hardware de la Pi aquí
    }
    kfree_skb(skb);
    return NET_RX_SUCCESS;
}

static int __init mi_driver_init(void) {
    printk(KERN_INFO "[Mi_Driver_Proyecto] Inicializando sistema de transferencia de imágenes...\n");
    
    // Registrar el dispositivo en /dev/ de forma dinámica
    major_number = register_chrdev(0, DEVICE_NAME, &fops);
    if (major_number < 0) {
        printk(KERN_ERR "[Mi_Driver_Proyecto] Falló el registro del dispositivo de caracteres.\n");
        return major_number;
    }
    printk(KERN_INFO "[Mi_Driver_Proyecto] Dispositivo creado. Ejecutar: 'sudo mknod /dev/%s c %d 0' para activarlo.\n", DEVICE_NAME, major_number);
    
    // Registrar el hook de red universal que ya nos funciona
    mi_packet_hook.type = htons(ETH_P_ALL);
    mi_packet_hook.func = mi_packet_rcv;
    dev_add_pack(&mi_packet_hook);
    
    return 0;
}

static void __exit mi_driver_exit(void) {
    dev_remove_pack(&mi_packet_hook);
    unregister_chrdev(major_number, DEVICE_NAME);
    printk(KERN_INFO "[Mi_Driver_Proyecto] Driver de imágenes descargado del sistema.\n");
}

module_init(mi_driver_init);
module_exit(mi_driver_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("PB Team");
MODULE_DESCRIPTION("Driver de Kernel a la medida para transferencia de imágenes por OTG");
