# SPDX-License-Identifier: GPL-2.0
#
# Makefile for the linux kernel.
#

obj-y += si_core/
obj-$(CONFIG_QCOM_SCM)		+= qcom-scm.o
qcom-scm-objs += qcom_scm.o qcom_scm-smc.o qcom_scm-legacy.o
obj-$(CONFIG_QCOM_QSEECOM)	+= qcom_qseecom.o
obj-$(CONFIG_QCOM_QSEECOM_UEFISECAPP) += qcom_qseecom_uefisecapp.o

qcom-scm-objs-$(CONFIG_QTEE_SHM_BRIDGE) += qtee_shmbridge.o
qcom-scm-objs += $(qcom-scm-objs-y)
