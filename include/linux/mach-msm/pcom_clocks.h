#define PCOM_ACPU_CLK	0   /* Applications processor clock */
#define PCOM_ADM_CLK	1   /* Applications data mover clock */
#define PCOM_ADSP_CLK	2   /* ADSP clock */
#define PCOM_EBI1_CLK	3   /* External bus interface 1 clock */
#define PCOM_EBI2_CLK	4   /* External bus interface 2 clock */
#define PCOM_ECODEC_CLK	5   /* External CODEC clock */
#define PCOM_EMDH_CLK	6   /* External MDDI host clock */
#define PCOM_GP_CLK	7   /* General purpose clock */
#define PCOM_GRP_3D_CLK	8   /* Graphics clock */
#define PCOM_I2C_CLK	9   /* I2C clock */
#define PCOM_ICODEC_RX_CLK	10  /* Internal CODEX RX clock */
#define PCOM_ICODEC_TX_CLK	11  /* Internal CODEX TX clock */
#define PCOM_IMEM_CLK	12  /* Internal graphics memory clock */
#define PCOM_MDC_CLK	13  /* MDDI client clock */
#define PCOM_MDP_CLK	14  /* Mobile display processor clock */
#define PCOM_PBUS_CLK	15  /* Peripheral bus clock */
#define PCOM_PCM_CLK	16  /* PCM clock */
#define PCOM_PMDH_CLK	17  /* Primary MDDI host clock */
#define PCOM_SDAC_CLK	18  /* Stereo DAC clock */
#define PCOM_SDC1_CLK	19  /* Secure Digital Card clocks */
#define PCOM_SDC1_P_CLK	20
#define PCOM_SDC2_CLK	21
#define PCOM_SDC2_P_CLK	22
#define PCOM_SDC3_CLK	23
#define PCOM_SDC3_P_CLK	24
#define PCOM_SDC4_CLK	25
#define PCOM_SDC4_P_CLK	26
#define PCOM_TSIF_CLK	27  /* Transport Stream Interface clocks */
#define PCOM_TSIF_REF_CLK	28
#define PCOM_TV_DAC_CLK	29  /* TV clocks */
#define PCOM_TV_ENC_CLK	30
#define PCOM_UART1_CLK	31  /* UART clocks */
#define PCOM_UART2_CLK	32
#define PCOM_UART3_CLK	33
#define PCOM_UART1DM_CLK	34
#define PCOM_UART2DM_CLK	35
#define PCOM_USB_HS_CLK	36  /* High speed USB core clock */
#define PCOM_USB_HS_P_CLK	37  /* High speed USB pbus clock */
#define PCOM_USB_OTG_CLK	38  /* Full speed USB clock */
#define PCOM_VDC_CLK	39  /* Video controller clock */
#define PCOM_VFE_MDC_CLK	40  /* Camera / Video Front End clock */
#define PCOM_VFE_CLK	41  /* VFE MDDI client clock */
#define PCOM_MDP_LCDC_PCLK_CLK	42
#define PCOM_MDP_LCDC_PAD_PCLK_CLK 43
#define PCOM_MDP_VSYNC_CLK	44
#define PCOM_SPI_CLK	45
#define PCOM_VFE_AXI_CLK	46
#define PCOM_USB_HS2_CLK	47  /* High speed USB 2 core clock */
#define PCOM_USB_HS2_P_CLK	48  /* High speed USB 2 pbus clock */
#define PCOM_USB_HS3_CLK	49  /* High speed USB 3 core clock */
#define PCOM_USB_HS3_P_CLK	50  /* High speed USB 3 pbus clock */
#define PCOM_GRP_3D_P_CLK	51  /* Graphics pbus clock */
#define PCOM_USB_PHY_CLK	52  /* USB PHY clock */
#define PCOM_USB_HS_CORE_CLK	53  /* High speed USB 1 core clock */
#define PCOM_USB_HS2_CORE_CLK	54  /* High speed USB 2 core clock */
#define PCOM_USB_HS3_CORE_CLK	55  /* High speed USB 3 core clock */
#define PCOM_CAM_M_CLK		56
#define PCOM_CAMIF_PAD_P_CLK	57
#define PCOM_GRP_2D_CLK		58
#define PCOM_GRP_2D_P_CLK		59
#define PCOM_I2S_CLK		60
#define PCOM_JPEG_CLK		61
#define PCOM_JPEG_P_CLK		62
#define PCOM_LPA_CODEC_CLK		63
#define PCOM_LPA_CORE_CLK		64
#define PCOM_LPA_P_CLK		65
#define PCOM_MDC_IO_CLK		66
#define PCOM_MDC_P_CLK		67
#define PCOM_MFC_CLK		68
#define PCOM_MFC_DIV2_CLK		69
#define PCOM_MFC_P_CLK		70
#define PCOM_QUP_I2C_CLK		71
#define PCOM_ROTATOR_IMEM_CLK	72
#define PCOM_ROTATOR_P_CLK		73
#define PCOM_VFE_CAMIF_CLK		74
#define PCOM_VFE_P_CLK		75
#define PCOM_VPE_CLK		76
#define PCOM_I2C_2_CLK		77
#define PCOM_MI2S_CODEC_RX_S_CLK	78
#define PCOM_MI2S_CODEC_RX_M_CLK	79
#define PCOM_MI2S_CODEC_TX_S_CLK	80
#define PCOM_MI2S_CODEC_TX_M_CLK	81
#define PCOM_PMDH_P_CLK		82
#define PCOM_EMDH_P_CLK		83
#define PCOM_SPI_P_CLK		84
#define PCOM_TSIF_P_CLK		85
#define PCOM_MDP_P_CLK		86
#define PCOM_SDAC_M_CLK		87
#define PCOM_MI2S_S_CLK		88
#define PCOM_MI2S_M_CLK		89
#define PCOM_AXI_ROTATOR_CLK	90
#define PCOM_HDMI_CLK		91
#define PCOM_CSI0_CLK		92
#define PCOM_CSI0_VFE_CLK		93
#define PCOM_CSI0_P_CLK		94
#define PCOM_CSI1_CLK		95
#define PCOM_CSI1_VFE_CLK		96
#define PCOM_CSI1_P_CLK		97
#define PCOM_GSBI_CLK		98
#define PCOM_GSBI_P_CLK		99
#define PCOM_CE_CLK		100 /* Crypto engine */
#define PCOM_CODEC_SSBI_CLK	101
#define PCOM_TCXO_DIV4_CLK		102
#define PCOM_GSBI1_QUP_CLK		103
#define PCOM_GSBI2_QUP_CLK		104
#define PCOM_GSBI1_QUP_P_CLK	105
#define PCOM_GSBI2_QUP_P_CLK	106
#define PCOM_DSI_CLK		107
#define PCOM_DSI_ESC_CLK		108
#define PCOM_DSI_PIXEL_CLK		109
#define PCOM_DSI_BYTE_CLK		110
#define PCOM_EBI1_FIXED_CLK	111 /* Not dropped during power-collapse */
#define PCOM_DSI_REF_CLK		112
#define PCOM_MDP_DSI_P_CLK		113
#define PCOM_AHB_M_CLK		114
#define PCOM_AHB_S_CLK		115

#define PCOM_NR_CLKS		116