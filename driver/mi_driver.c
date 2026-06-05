#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/netdevice.h>
#include <linux/skbuff.h>
#include <linux/fs.h>      // Necesario para registrar dispositivos en /dev/
#include <linux/uaccess.h> // Necesario para pasar datos de Usuario a Kernel (copy_from_user)

#define DEVICE_NAME "raspberry_usb"
#define INTERFAZ_PI "enx*"

static int major_number;
static struct packet_type mi_packet_hook;

// Esta función se ejecuta cuando el usuario envía la imagen al archivo /dev/raspberry_usb
static ssize_t driver_write(struct file *file, const char __user *buffer, size_t len, loff_t *offset) {
    char *kernel_buffer;
    
    printk(KERN_INFO "[Mi_Driver_Proyecto] Recibiendo ráfaga de la imagen desde la aplicación del usuario. Tamaño: %zu bytes\n", len);
    
    // Reservar memoria segura en el Kernel para procesar los bytes de la imagen
    kernel_buffer = kmalloc(len, GFP_KERNEL);
    if (!kernel_buffer) {
        return -ENOMEM;
    }
    
    // Pasar los bytes de la imagen desde el espacio de usuario al espacio del kernel de forma segura
    if (copy_from_user(kernel_buffer, buffer, len)) {
        kfree(kernel_buffer);
        return -EFAULT;
    }
    
    /* 
       Aquí el driver encapsulará estos bytes en un buffer 
       y los inyectará directo al cable USB hacia la Raspberry Pi
    */
    
    printk(KERN_INFO "[Mi_Driver_Proyecto] Bytes de la imagen listos en Kernel Space para ser enviados a la Pi.\n");
    
    kfree(kernel_buffer);
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
