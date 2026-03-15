subdir-ccflags-y += -I$(srctree)/arch/arm/mach-$(MTK_PLATFORM)/$(ARCH_MTK_PROJECT)/dct/dct
#subdir-ccflags-y += -DWMT_PLAT_ALPS=1

COMBO_CHIP_SUPPORT := false
ifneq ($(filter MT6620E3,$(CONFIG_MTK_COMBO_CHIP)),)
	COMBO_CHIP_SUPPORT := true
endif
ifneq ($(filter MT6628,$(CONFIG_MTK_COMBO_CHIP)),)
	COMBO_CHIP_SUPPORT := true
endif
ifneq ($(filter MT6630,$(CONFIG_MTK_COMBO_CHIP)),)
	COMBO_CHIP_SUPPORT := true
endif
ifneq ($(filter MT6632,$(CONFIG_MTK_COMBO_CHIP)),)
	COMBO_CHIP_SUPPORT := true
endif
ifeq ($(COMBO_CHIP_SUPPORT), true)
	subdir-ccflags-y += -D MTK_WCN_COMBO_CHIP_SUPPORT
endif

ifneq ($(filter CONSYS_%,$(CONFIG_MTK_COMBO_CHIP)),)
	subdir-ccflags-y += -D MTK_WCN_SOC_CHIP_SUPPORT
endif

	ccflags-y += -I$(src)/../common_main/linux/include

ifeq ($(CONFIG_MTK_COMBO),y)
	ccflags-y += -I$(src)/drv_init/inc
	obj-y += mtk_wcn_stub_alps.o
	obj-y += wmt_gpio.o

	obj-y += wmt_detect.o
	obj-y += sdio_detect.o
	obj-y += wmt_detect_pwr.o

	obj-y += drv_init/
endif

ifeq ($(CONFIG_MTK_COMBO),m)
	ccflags-y += -I$(src)/drv_init/inc

	obj-y += mtk_wcn_stub_alps.o
	obj-y += wmt_gpio.o

	obj-$(CONFIG_MTK_COMBO) += mtk_wmt_detect.o
	mtk_wmt_detect-objs := wmt_detect.o
	mtk_wmt_detect-objs += drv_init/conn_drv_init.o drv_init/common_drv_init.o
	mtk_wmt_detect-objs += drv_init/bluetooth_drv_init.o drv_init/gps_drv_init.o
	mtk_wmt_detect-objs += drv_init/fm_drv_init.o
	mtk_wmt_detect-objs += sdio_detect.o
	mtk_wmt_detect-objs += wmt_gpio.o
	mtk_wmt_detect-objs += wmt_detect_pwr.o
	mtk_wmt_detect-objs += mtk_wcn_stub_alps.o
endif
