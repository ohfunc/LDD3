#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/stat.h>

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Deez Nuts");
MODULE_DESCRIPTION("Prints 'Deez Nuts'.");
MODULE_VERSION("69.420");
MODULE_ALIAS("nutz");

static int how_many_nuts = 1;
module_param(how_many_nuts, int, S_IRUGO);

static int __init hello_init(void) {
	for (int i = 0; i < how_many_nuts; ++i) {
		printk(KERN_ALERT "Deez Nuts");
	}
	return 0;
}

static void __exit hello_exit(void) {
	printk(KERN_ALERT "Goodbye world :(\n");
}

module_init(hello_init);
module_exit(hello_exit);


