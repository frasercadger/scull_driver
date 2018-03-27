#include <linux/init.h>
#include <linux/module.h>

static int __init scull_init(void)
{
	/* TODO: Add init code */
	return 0;
}
module_init(scull_init);

static void __exit scull_exit(void)
{
	/* TODO: Add exit code */
}
module_exit(scull_exit);
