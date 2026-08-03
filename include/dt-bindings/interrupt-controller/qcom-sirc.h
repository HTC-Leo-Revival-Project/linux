/* SPDX-License-Identifier: GPL-2.0 OR MIT */
/*
 * This header provides constants for the ARM GIC.
 */

#ifndef _DT_BINDINGS_INTERRUPT_CONTROLLER_QCOM_SIRC_H
#define _DT_BINDINGS_INTERRUPT_CONTROLLER_QCOM_SIRC_H

#include <dt-bindings/interrupt-controller/irq.h>

#define SIRC_INT_SELECT          0x00
#define SIRC_INT_ENABLE          0x04
#define SIRC_INT_ENABLE_CLEAR    0x08
#define SIRC_INT_ENABLE_SET      0x0C
#define SIRC_INT_TYPE            0x10
#define SIRC_INT_POLARITY        0x14
#define SIRC_SECURITY            0x18
#define SIRC_IRQ_STATUS          0x1C
#define SIRC_IRQ1_STATUS         0x20
#define SIRC_RAW_STATUS          0x24
#define SIRC_INT_CLEAR           0x28
#define SIRC_SOFT_INT            0x2C

#define NUM_SIRC_REGS 2

#endif
