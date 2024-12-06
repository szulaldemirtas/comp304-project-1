#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/list.h>
#include <linux/module.h>
#include <linux/pid.h>
#include <linux/sched.h>
#include <linux/slab.h>

// Meta Information
MODULE_LICENSE("GPL");
MODULE_AUTHOR("type here your own name");
MODULE_DESCRIPTION("A module that knows how to greet");

char *name;
int age;

/*
 * module_param(foo, int, 0000)
 * The first param is the parameters name
 * The second param is its data type
 * The final argument is the permissions bits,
 * for exposing parameters in sysfs (if non-zero) at a later stage.
 */

module_param(name, charp, S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH);
MODULE_PARM_DESC(name, "name of the caller");

module_param(age, int, S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH);
MODULE_PARM_DESC(age, "age of the caller");

// A function that runs when the module is first loaded
int simple_init(void) {
	struct task_struct *ts;

	ts = get_pid_task(find_get_pid(4), PIDTYPE_PID);

	printk("Hello from the kernel, user: %s, age: %d\n", name, age);
	printk("command: %s\n", ts->comm);
	return 0;
}

// A function that runs when the module is removed
void simple_exit(void) {
	printk("Goodbye from the kernel, user: %s, age: %d\n", name, age);
}

module_init(simple_init);
module_exit(simple_exit);
