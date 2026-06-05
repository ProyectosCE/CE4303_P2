#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/usb.h>

// Se define un Vendor ID y Product ID ficticios para luego configurarlos en la rasb
// Esto asegura que NO use ningún driver comercial del sistema
#define MY_VENDOR_ID  0x2e8a
#define MY_PRODUCT_ID 0x00FF

// Tabla de dispositivos compatibles para que el Kernel asocie este driver a la Pi
static const struct usb_device_id mi_usb_table[] = {
    { USB_DEVICE(MY_VENDOR_ID, MY_PRODUCT_ID) },
    { } /* Entrada vacía para terminar la estructura */
};
MODULE_DEVICE_TABLE(usb, mi_usb_table);

// Función Probe: Se ejecuta automáticamente cuando conectas la Pi por USB
static int mi_usb_probe(struct usb_interface *interface, const struct usb_device_id *id) {
    printk(KERN_INFO "[Mi_Driver] ¡Raspberry Pi detectada con éxito por el driver personalizado!\n");
    /* Aquí se registran las primitivas de lectura/escritura más adelante */
    return 0;
}

// Función Disconnect: Se ejecuta automáticamente cuando desconectas la Pi
static void mi_usb_disconnect(struct usb_interface *interface) {
    printk(KERN_INFO "[Mi_Driver] Raspberry Pi desconectada. Liberando recursos.\n");
}

// Estructura principal del Driver USB
static struct usb_driver mi_personal_driver = {
    .name = "mi_driver_custom",
    .id_table = mi_usb_table,
    .probe = mi_usb_probe,
    .disconnect = mi_usb_disconnect,
};

// Función de inicialización al cargar el módulo (lsmod)
static int __init mi_driver_init(void) {
    int result;
    printk(KERN_INFO "[Mi_Driver] Registrando el Driver USB en el Kernel de Linux...\n");
    result = usb_register(&mi_personal_driver);
    if (result < 0) {
        printk(KERN_ERR "[Mi_Driver] Error al registrar el driver. Código: %d\n", result);
        return result;
    }
    return 0;
}

// Función de salida al descargar el módulo
static void __exit mi_driver_exit(void) {
    printk(KERN_INFO "[Mi_Driver] Descargando el Driver USB del sistema...\n");
    usb_deregister(&mi_personal_driver);
}

// Macros obligatorias de Licencia y Autoría para que el Kernel no tire advertencias
module_init(mi_driver_init);
module_exit(mi_driver_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Jose Barquero");
MODULE_DESCRIPTION("Driver USB personalizado para proyecto de cátedra");
