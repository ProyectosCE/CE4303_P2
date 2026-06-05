#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/netdevice.h>
#include <linux/skbuff.h>
#include <linux/if_arp.h> // Añadir la cabecera para inspección de hardware

static struct packet_type mi_packet_hook;

// Esta función intercepta los bytes crudos de forma inteligente
static int mi_packet_rcv(struct sk_buff *skb, struct net_device *dev, 
                         struct packet_type *pt, struct net_device *orig_dev) {
    
    // Filtro inteligente: Si el nombre de la interfaz empieza con "enx" (que son las de la Pi)
    // interceptar el paquete sin importar el resto de números aleatorios
    if (strncmp(dev->name, "enx", 3) == 0) {
        printk(KERN_INFO "[Mi_Driver_Red] ¡Paquete interceptado en interfaz %s! Tamaño: %d bytes\n", dev->name, skb->len);
    }
    
    kfree_skb(skb);
    return NET_RX_SUCCESS;
}

// Inicialización del módulo (lsmod)
static int __init mi_driver_init(void) {
    printk(KERN_INFO "[Mi_Driver_Red] Cargando Driver personalizado Universal...\n");
    
    mi_packet_hook.type = htons(ETH_P_ALL);
    mi_packet_hook.func = mi_packet_rcv;
    dev_add_pack(&mi_packet_hook);
    
    printk(KERN_INFO "[Mi_Driver_Red] Driver Universal registrado con éxito.\n");
    return 0;
}

static void __exit mi_driver_exit(void) {
    printk(KERN_INFO "[Mi_Driver_Red] Descargando el Driver del sistema...\n");
    dev_remove_pack(&mi_packet_hook);
}

module_init(mi_driver_init);
module_exit(mi_driver_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Jose Barquero");
MODULE_DESCRIPTION("Driver de Kernel Universal para interceptar gadgets USB en Ubuntu");
