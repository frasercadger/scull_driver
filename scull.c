/*
    Copyright: Fraser Cadger (2018) <frasercadger@gmail.com>

    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program.  If not, see <http://www.gnu.org/licenses/>.

    Description: Simple Character Utiltity for Loading Localities (scull)
    driver.
*/

/* Includes */
#include <linux/init.h>
#include <linux/module.h>

/* Function prototypes */

/* Function definitions */

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
