/**
 * @file lan8651_regs.h
 * @brief LAN8651 10BASE-T1S MAC-PHY Register Definitions
 * 
 * Complete register address and bitfield definitions for Microchip LAN8650/1
 * 10BASE-T1S MAC-PHY Controller with SPI interface.
 * 
 * Based on: Official Microchip LAN8650/1 Datasheet
 * Reference: https://onlinedocs.microchip.com/oxy/GUID-7A87AF7C-8456-416F-A89B-41F172C54117-en-US-10/
 * 
 * Memory Map Selector (MMS) Architecture:
 * - Upper 16 bits = MMS (Memory Map Selector)
 * - Lower 16 bits = Register offset within MMS
 * 
 * @date March 7, 2026
 * @version 1.0
 */

#ifndef LAN8651_REGS_H
#define LAN8651_REGS_H

#include <stdint.h>

/*================================================================================================*/
/*                                    REGISTER ADDRESSES                                         */
/*================================================================================================*/

/* MMS_0: Open Alliance Standard Registers */
#define LAN8651_OA_ID                   0x00000000U     /* Open Alliance ID Register */
#define LAN8651_OA_PHYID                0x00000001U     /* Open Alliance PHY ID Register */
#define LAN8651_OA_STDCAP               0x00000002U     /* Standard Capabilities Register */
#define LAN8651_OA_RESET                0x00000003U     /* Reset Control and Status Register */
#define LAN8651_OA_CONFIG0              0x00000004U     /* Configuration Register 0 */
#define LAN8651_OA_CONFIG1              0x00000005U     /* Configuration Register 1 */
#define LAN8651_OA_STATUS0              0x00000008U     /* Status Register 0 */
#define LAN8651_OA_STATUS1              0x00000009U     /* Status Register 1 */
#define LAN8651_OA_BUFSTS               0x0000000BU     /* Buffer Status Register */
#define LAN8651_OA_IMASK0               0x0000000CU     /* Interrupt Mask Register 0 */
#define LAN8651_OA_IMASK1               0x0000000DU     /* Interrupt Mask Register 1 */

/* Timestamp Capture Registers */
#define LAN8651_TTSCAH                  0x00000010U     /* Transmit Timestamp Capture A (High) */
#define LAN8651_TTSCAL                  0x00000011U     /* Transmit Timestamp Capture A (Low) */
#define LAN8651_TTSCBH                  0x00000012U     /* Transmit Timestamp Capture B (High) */
#define LAN8651_TTSCBL                  0x00000013U     /* Transmit Timestamp Capture B (Low) */
#define LAN8651_TTSCCH                  0x00000014U     /* Transmit Timestamp Capture C (High) */
#define LAN8651_TTSCCL                  0x00000015U     /* Transmit Timestamp Capture C (Low) */

/* PHY Clause 22 Registers (MMS_0 with 0xFF00 offset) */
#define LAN8651_PHY_BASIC_CONTROL       0x0000FF00U     /* PHY Basic Control Register */
#define LAN8651_PHY_BASIC_STATUS        0x0000FF01U     /* PHY Basic Status Register */
#define LAN8651_PHY_ID1                 0x0000FF02U     /* PHY Identifier Register 1 */
#define LAN8651_PHY_ID2                 0x0000FF03U     /* PHY Identifier Register 2 */
#define LAN8651_PHY_MMDCTRL             0x0000FF0DU     /* MMD Access Control Register */
#define LAN8651_PHY_MMDAD               0x0000FF0EU     /* MMD Access Address/Data Register */

/*================================================================================================*/
/*                                OA_ID REGISTER BITFIELDS                                       */
/*================================================================================================*/
/* OA_ID Register (0x00000000) - Open Alliance ID Register */
#define LAN8651_OA_ID_MAJVER_MASK       0x000000F0U
#define LAN8651_OA_ID_MAJVER_SHIFT      4U
#define LAN8651_OA_ID_MINVER_MASK       0x0000000FU
#define LAN8651_OA_ID_MINVER_SHIFT      0U

/*================================================================================================*/
/*                               OA_PHYID REGISTER BITFIELDS                                     */
/*================================================================================================*/
/* OA_PHYID Register (0x00000001) - Open Alliance PHY ID Register */
#define LAN8651_OA_PHYID_OUI_21_14_MASK     0xFF000000U
#define LAN8651_OA_PHYID_OUI_21_14_SHIFT    24U
#define LAN8651_OA_PHYID_OUI_13_6_MASK      0x00FF0000U
#define LAN8651_OA_PHYID_OUI_13_6_SHIFT     16U
#define LAN8651_OA_PHYID_OUI_5_0_MASK       0x0000FC00U
#define LAN8651_OA_PHYID_OUI_5_0_SHIFT      10U
#define LAN8651_OA_PHYID_MODEL_5_4_MASK     0x00000300U
#define LAN8651_OA_PHYID_MODEL_5_4_SHIFT    8U
#define LAN8651_OA_PHYID_MODEL_3_0_MASK     0x000000F0U
#define LAN8651_OA_PHYID_MODEL_3_0_SHIFT    4U
#define LAN8651_OA_PHYID_REVISION_MASK      0x0000000FU
#define LAN8651_OA_PHYID_REVISION_SHIFT     0U

/*================================================================================================*/
/*                              OA_STDCAP REGISTER BITFIELDS                                     */
/*================================================================================================*/
/* OA_STDCAP Register (0x00000002) - Standard Capabilities Register */
#define LAN8651_OA_STDCAP_TXFCSVC_MASK      0x00000400U
#define LAN8651_OA_STDCAP_TXFCSVC_SHIFT     10U
#define LAN8651_OA_STDCAP_IPRAC_MASK        0x00000200U
#define LAN8651_OA_STDCAP_IPRAC_SHIFT       9U
#define LAN8651_OA_STDCAP_DPRAC_MASK        0x00000100U
#define LAN8651_OA_STDCAP_DPRAC_SHIFT       8U
#define LAN8651_OA_STDCAP_CTC_MASK          0x00000080U
#define LAN8651_OA_STDCAP_CTC_SHIFT         7U
#define LAN8651_OA_STDCAP_FTSC_MASK         0x00000040U
#define LAN8651_OA_STDCAP_FTSC_SHIFT        6U
#define LAN8651_OA_STDCAP_AIDC_MASK         0x00000020U
#define LAN8651_OA_STDCAP_AIDC_SHIFT        5U
#define LAN8651_OA_STDCAP_SEQC_MASK         0x00000010U
#define LAN8651_OA_STDCAP_SEQC_SHIFT        4U
#define LAN8651_OA_STDCAP_MINBPS_MASK       0x00000007U
#define LAN8651_OA_STDCAP_MINBPS_SHIFT      0U

/*================================================================================================*/
/*                               OA_RESET REGISTER BITFIELDS                                     */
/*================================================================================================*/
/* OA_RESET Register (0x00000003) - Reset Control and Status Register */
#define LAN8651_OA_RESET_SWRESET_MASK       0x00000001U
#define LAN8651_OA_RESET_SWRESET_SHIFT      0U

/*================================================================================================*/
/*                              OA_CONFIG0 REGISTER BITFIELDS                                    */
/*================================================================================================*/
/* OA_CONFIG0 Register (0x00000004) - Configuration Register 0 */
#define LAN8651_OA_CONFIG0_SYNC_MASK        0x00008000U
#define LAN8651_OA_CONFIG0_SYNC_SHIFT       15U
#define LAN8651_OA_CONFIG0_TXFCSVE_MASK     0x00004000U
#define LAN8651_OA_CONFIG0_TXFCSVE_SHIFT    14U
#define LAN8651_OA_CONFIG0_RFA_MASK         0x00003000U
#define LAN8651_OA_CONFIG0_RFA_SHIFT        12U
#define LAN8651_OA_CONFIG0_TXCTHRESH_MASK   0x00000C00U
#define LAN8651_OA_CONFIG0_TXCTHRESH_SHIFT  10U
#define LAN8651_OA_CONFIG0_TXCTE_MASK       0x00000200U
#define LAN8651_OA_CONFIG0_TXCTE_SHIFT      9U
#define LAN8651_OA_CONFIG0_RXCTE_MASK       0x00000100U
#define LAN8651_OA_CONFIG0_RXCTE_SHIFT      8U
#define LAN8651_OA_CONFIG0_FTSE_MASK        0x00000080U
#define LAN8651_OA_CONFIG0_FTSE_SHIFT       7U
#define LAN8651_OA_CONFIG0_FTSS_MASK        0x00000040U
#define LAN8651_OA_CONFIG0_FTSS_SHIFT       6U
#define LAN8651_OA_CONFIG0_PROTE_MASK       0x00000020U
#define LAN8651_OA_CONFIG0_PROTE_SHIFT      5U
#define LAN8651_OA_CONFIG0_SEQE_MASK        0x00000010U
#define LAN8651_OA_CONFIG0_SEQE_SHIFT       4U
#define LAN8651_OA_CONFIG0_BPS_MASK         0x00000007U
#define LAN8651_OA_CONFIG0_BPS_SHIFT        0U

/*================================================================================================*/
/*                              OA_STATUS0 REGISTER BITFIELDS                                    */
/*================================================================================================*/
/* OA_STATUS0 Register (0x00000008) - Status Register 0 */
#define LAN8651_OA_STATUS0_CPDE_MASK        0x00001000U
#define LAN8651_OA_STATUS0_CPDE_SHIFT       12U
#define LAN8651_OA_STATUS0_TXFCSE_MASK      0x00000800U
#define LAN8651_OA_STATUS0_TXFCSE_SHIFT     11U
#define LAN8651_OA_STATUS0_TTSCAC_MASK      0x00000400U
#define LAN8651_OA_STATUS0_TTSCAC_SHIFT     10U
#define LAN8651_OA_STATUS0_TTSCAB_MASK      0x00000200U
#define LAN8651_OA_STATUS0_TTSCAB_SHIFT     9U
#define LAN8651_OA_STATUS0_TTSCAA_MASK      0x00000100U
#define LAN8651_OA_STATUS0_TTSCAA_SHIFT     8U
#define LAN8651_OA_STATUS0_PHYINT_MASK      0x00000080U
#define LAN8651_OA_STATUS0_PHYINT_SHIFT     7U
#define LAN8651_OA_STATUS0_RESETC_MASK      0x00000040U
#define LAN8651_OA_STATUS0_RESETC_SHIFT     6U
#define LAN8651_OA_STATUS0_HDRE_MASK        0x00000020U
#define LAN8651_OA_STATUS0_HDRE_SHIFT       5U
#define LAN8651_OA_STATUS0_LOFE_MASK        0x00000010U
#define LAN8651_OA_STATUS0_LOFE_SHIFT       4U
#define LAN8651_OA_STATUS0_RXBOE_MASK       0x00000008U
#define LAN8651_OA_STATUS0_RXBOE_SHIFT      3U
#define LAN8651_OA_STATUS0_TXBUE_MASK       0x00000004U
#define LAN8651_OA_STATUS0_TXBUE_SHIFT      2U
#define LAN8651_OA_STATUS0_TXBOE_MASK       0x00000002U
#define LAN8651_OA_STATUS0_TXBOE_SHIFT      1U
#define LAN8651_OA_STATUS0_TXPE_MASK        0x00000001U
#define LAN8651_OA_STATUS0_TXPE_SHIFT       0U

/*================================================================================================*/
/*                              OA_STATUS1 REGISTER BITFIELDS                                    */
/*================================================================================================*/
/* OA_STATUS1 Register (0x00000009) - Status Register 1 */
#define LAN8651_OA_STATUS1_SEV_MASK         0x10000000U
#define LAN8651_OA_STATUS1_SEV_SHIFT        28U
#define LAN8651_OA_STATUS1_TTSCMC_MASK      0x04000000U
#define LAN8651_OA_STATUS1_TTSCMC_SHIFT     26U
#define LAN8651_OA_STATUS1_TTSCMB_MASK      0x02000000U
#define LAN8651_OA_STATUS1_TTSCMB_SHIFT     25U
#define LAN8651_OA_STATUS1_TTSCMA_MASK      0x01000000U
#define LAN8651_OA_STATUS1_TTSCMA_SHIFT     24U
#define LAN8651_OA_STATUS1_TTSCOFC_MASK     0x00800000U
#define LAN8651_OA_STATUS1_TTSCOFC_SHIFT    23U
#define LAN8651_OA_STATUS1_TTSCOFB_MASK     0x00400000U
#define LAN8651_OA_STATUS1_TTSCOFB_SHIFT    22U
#define LAN8651_OA_STATUS1_TTSCOFA_MASK     0x00200000U
#define LAN8651_OA_STATUS1_TTSCOFA_SHIFT    21U
#define LAN8651_OA_STATUS1_BUSER_MASK       0x00100000U
#define LAN8651_OA_STATUS1_BUSER_SHIFT      20U
#define LAN8651_OA_STATUS1_UV18_MASK        0x00080000U
#define LAN8651_OA_STATUS1_UV18_SHIFT       19U
#define LAN8651_OA_STATUS1_ECC_MASK         0x00040000U
#define LAN8651_OA_STATUS1_ECC_SHIFT        18U
#define LAN8651_OA_STATUS1_FSMSTER_MASK     0x00020000U
#define LAN8651_OA_STATUS1_FSMSTER_SHIFT    17U
#define LAN8651_OA_STATUS1_TXNER_MASK       0x00000002U
#define LAN8651_OA_STATUS1_TXNER_SHIFT      1U
#define LAN8651_OA_STATUS1_RXNER_MASK       0x00000001U
#define LAN8651_OA_STATUS1_RXNER_SHIFT      0U

/*================================================================================================*/
/*                              OA_BUFSTS REGISTER BITFIELDS                                     */
/*================================================================================================*/
/* OA_BUFSTS Register (0x0000000B) - Buffer Status Register */
#define LAN8651_OA_BUFSTS_TXC_MASK          0x0000FF00U
#define LAN8651_OA_BUFSTS_TXC_SHIFT         8U
#define LAN8651_OA_BUFSTS_RBA_MASK          0x000000FFU
#define LAN8651_OA_BUFSTS_RBA_SHIFT         0U

/*================================================================================================*/
/*                              OA_IMASK0 REGISTER BITFIELDS                                     */
/*================================================================================================*/
/* OA_IMASK0 Register (0x0000000C) - Interrupt Mask Register 0 */
#define LAN8651_OA_IMASK0_CPDEM_MASK        0x00001000U
#define LAN8651_OA_IMASK0_CPDEM_SHIFT       12U
#define LAN8651_OA_IMASK0_TXFCSEM_MASK      0x00000800U
#define LAN8651_OA_IMASK0_TXFCSEM_SHIFT     11U
#define LAN8651_OA_IMASK0_TTSCACM_MASK      0x00000400U
#define LAN8651_OA_IMASK0_TTSCACM_SHIFT     10U
#define LAN8651_OA_IMASK0_TTSCABM_MASK      0x00000200U
#define LAN8651_OA_IMASK0_TTSCABM_SHIFT     9U
#define LAN8651_OA_IMASK0_TTSCAAM_MASK      0x00000100U
#define LAN8651_OA_IMASK0_TTSCAAM_SHIFT     8U
#define LAN8651_OA_IMASK0_PHYINTM_MASK      0x00000080U
#define LAN8651_OA_IMASK0_PHYINTM_SHIFT     7U
#define LAN8651_OA_IMASK0_RESETCM_MASK      0x00000040U
#define LAN8651_OA_IMASK0_RESETCM_SHIFT     6U
#define LAN8651_OA_IMASK0_HDREM_MASK        0x00000020U
#define LAN8651_OA_IMASK0_HDREM_SHIFT       5U
#define LAN8651_OA_IMASK0_LOFEM_MASK        0x00000010U
#define LAN8651_OA_IMASK0_LOFEM_SHIFT       4U
#define LAN8651_OA_IMASK0_RXBOEM_MASK       0x00000008U
#define LAN8651_OA_IMASK0_RXBOEM_SHIFT      3U
#define LAN8651_OA_IMASK0_TXBUEM_MASK       0x00000004U
#define LAN8651_OA_IMASK0_TXBUEM_SHIFT      2U
#define LAN8651_OA_IMASK0_TXBOEM_MASK       0x00000002U
#define LAN8651_OA_IMASK0_TXBOEM_SHIFT      1U
#define LAN8651_OA_IMASK0_TXPEM_MASK        0x00000001U
#define LAN8651_OA_IMASK0_TXPEM_SHIFT       0U

/*================================================================================================*/
/*                              OA_IMASK1 REGISTER BITFIELDS                                     */
/*================================================================================================*/
/* OA_IMASK1 Register (0x0000000D) - Interrupt Mask Register 1 */
#define LAN8651_OA_IMASK1_SEVM_MASK         0x10000000U
#define LAN8651_OA_IMASK1_SEVM_SHIFT        28U
#define LAN8651_OA_IMASK1_TTSCMCM_MASK      0x04000000U
#define LAN8651_OA_IMASK1_TTSCMCM_SHIFT     26U
#define LAN8651_OA_IMASK1_TTSCMBM_MASK      0x02000000U
#define LAN8651_OA_IMASK1_TTSCMBM_SHIFT     25U
#define LAN8651_OA_IMASK1_TTSCMAM_MASK      0x01000000U
#define LAN8651_OA_IMASK1_TTSCMAM_SHIFT     24U
#define LAN8651_OA_IMASK1_TTSCOFCM_MASK     0x00800000U
#define LAN8651_OA_IMASK1_TTSCOFCM_SHIFT    23U
#define LAN8651_OA_IMASK1_TTSCOFBM_MASK     0x00400000U
#define LAN8651_OA_IMASK1_TTSCOFBM_SHIFT    22U
#define LAN8651_OA_IMASK1_TTSCOFAM_MASK     0x00200000U
#define LAN8651_OA_IMASK1_TTSCOFAM_SHIFT    21U
#define LAN8651_OA_IMASK1_BUSERM_MASK       0x00100000U
#define LAN8651_OA_IMASK1_BUSERM_SHIFT      20U
#define LAN8651_OA_IMASK1_UV18M_MASK        0x00080000U
#define LAN8651_OA_IMASK1_UV18M_SHIFT       19U
#define LAN8651_OA_IMASK1_ECCM_MASK         0x00040000U
#define LAN8651_OA_IMASK1_ECCM_SHIFT        18U
#define LAN8651_OA_IMASK1_FSMSTERM_MASK     0x00020000U
#define LAN8651_OA_IMASK1_FSMSTERM_SHIFT    17U
#define LAN8651_OA_IMASK1_TXNERM_MASK       0x00000002U
#define LAN8651_OA_IMASK1_TXNERM_SHIFT      1U
#define LAN8651_OA_IMASK1_RXNERM_MASK       0x00000001U
#define LAN8651_OA_IMASK1_RXNERM_SHIFT      0U

/*================================================================================================*/
/*                             TIMESTAMP REGISTER BITFIELDS                                      */
/*================================================================================================*/
/* Timestamp Capture Registers are 64-bit values split into HIGH and LOW 32-bit registers */
/* TTSCAH Register (0x00000010) - Transmit Timestamp Capture A (High) */
#define LAN8651_TTSCAH_TIMESTAMPA_63_56_MASK    0xFF000000U
#define LAN8651_TTSCAH_TIMESTAMPA_63_56_SHIFT   24U
#define LAN8651_TTSCAH_TIMESTAMPA_55_48_MASK    0x00FF0000U
#define LAN8651_TTSCAH_TIMESTAMPA_55_48_SHIFT   16U
#define LAN8651_TTSCAH_TIMESTAMPA_47_40_MASK    0x0000FF00U
#define LAN8651_TTSCAH_TIMESTAMPA_47_40_SHIFT   8U
#define LAN8651_TTSCAH_TIMESTAMPA_39_32_MASK    0x000000FFU
#define LAN8651_TTSCAH_TIMESTAMPA_39_32_SHIFT   0U

/* TTSCAL Register (0x00000011) - Transmit Timestamp Capture A (Low) */
#define LAN8651_TTSCAL_TIMESTAMPA_31_24_MASK    0xFF000000U
#define LAN8651_TTSCAL_TIMESTAMPA_31_24_SHIFT   24U
#define LAN8651_TTSCAL_TIMESTAMPA_23_16_MASK    0x00FF0000U
#define LAN8651_TTSCAL_TIMESTAMPA_23_16_SHIFT   16U
#define LAN8651_TTSCAL_TIMESTAMPA_15_8_MASK     0x0000FF00U
#define LAN8651_TTSCAL_TIMESTAMPA_15_8_SHIFT    8U
#define LAN8651_TTSCAL_TIMESTAMPA_7_0_MASK      0x000000FFU
#define LAN8651_TTSCAL_TIMESTAMPA_7_0_SHIFT     0U

/* Similar patterns apply for TTSCBH/L and TTSCCH/L registers */

/*================================================================================================*/
/*                            PHY BASIC_CONTROL REGISTER BITFIELDS                               */
/*================================================================================================*/
/* PHY_BASIC_CONTROL Register (0x0000FF00) - PHY Basic Control Register */
#define LAN8651_PHY_BASIC_CONTROL_SW_RESET_MASK     0x8000U
#define LAN8651_PHY_BASIC_CONTROL_SW_RESET_SHIFT    15U
#define LAN8651_PHY_BASIC_CONTROL_LOOPBACK_MASK     0x4000U
#define LAN8651_PHY_BASIC_CONTROL_LOOPBACK_SHIFT    14U
#define LAN8651_PHY_BASIC_CONTROL_SPD_SEL_0_MASK    0x2000U
#define LAN8651_PHY_BASIC_CONTROL_SPD_SEL_0_SHIFT   13U
#define LAN8651_PHY_BASIC_CONTROL_AUTONEGEN_MASK    0x1000U
#define LAN8651_PHY_BASIC_CONTROL_AUTONEGEN_SHIFT   12U
#define LAN8651_PHY_BASIC_CONTROL_PD_MASK           0x0800U
#define LAN8651_PHY_BASIC_CONTROL_PD_SHIFT          11U
#define LAN8651_PHY_BASIC_CONTROL_REAUTONEG_MASK    0x0200U
#define LAN8651_PHY_BASIC_CONTROL_REAUTONEG_SHIFT   9U
#define LAN8651_PHY_BASIC_CONTROL_DUPLEXMD_MASK     0x0100U
#define LAN8651_PHY_BASIC_CONTROL_DUPLEXMD_SHIFT    8U
#define LAN8651_PHY_BASIC_CONTROL_SPD_SEL_1_MASK    0x0040U
#define LAN8651_PHY_BASIC_CONTROL_SPD_SEL_1_SHIFT   6U

/*================================================================================================*/
/*                            PHY BASIC_STATUS REGISTER BITFIELDS                                */
/*================================================================================================*/
/* PHY_BASIC_STATUS Register (0x0000FF01) - PHY Basic Status Register */
#define LAN8651_PHY_BASIC_STATUS_100BT4A_MASK       0x8000U
#define LAN8651_PHY_BASIC_STATUS_100BT4A_SHIFT      15U
#define LAN8651_PHY_BASIC_STATUS_100BTXFDA_MASK     0x4000U
#define LAN8651_PHY_BASIC_STATUS_100BTXFDA_SHIFT    14U
#define LAN8651_PHY_BASIC_STATUS_100BTXHDA_MASK     0x2000U
#define LAN8651_PHY_BASIC_STATUS_100BTXHDA_SHIFT    13U
#define LAN8651_PHY_BASIC_STATUS_10BTFDA_MASK       0x1000U
#define LAN8651_PHY_BASIC_STATUS_10BTFDA_SHIFT      12U
#define LAN8651_PHY_BASIC_STATUS_10BTHDA_MASK       0x0800U
#define LAN8651_PHY_BASIC_STATUS_10BTHDA_SHIFT      11U
#define LAN8651_PHY_BASIC_STATUS_100BT2FDA_MASK     0x0400U
#define LAN8651_PHY_BASIC_STATUS_100BT2FDA_SHIFT    10U
#define LAN8651_PHY_BASIC_STATUS_100BT2HDA_MASK     0x0200U
#define LAN8651_PHY_BASIC_STATUS_100BT2HDA_SHIFT    9U
#define LAN8651_PHY_BASIC_STATUS_EXTSTS_MASK        0x0100U
#define LAN8651_PHY_BASIC_STATUS_EXTSTS_SHIFT       8U
#define LAN8651_PHY_BASIC_STATUS_AUTONEGC_MASK      0x0020U
#define LAN8651_PHY_BASIC_STATUS_AUTONEGC_SHIFT     5U
#define LAN8651_PHY_BASIC_STATUS_RMTFLTD_MASK       0x0010U
#define LAN8651_PHY_BASIC_STATUS_RMTFLTD_SHIFT      4U
#define LAN8651_PHY_BASIC_STATUS_AUTONEGA_MASK      0x0008U
#define LAN8651_PHY_BASIC_STATUS_AUTONEGA_SHIFT     3U
#define LAN8651_PHY_BASIC_STATUS_LNKSTS_MASK        0x0004U
#define LAN8651_PHY_BASIC_STATUS_LNKSTS_SHIFT       2U
#define LAN8651_PHY_BASIC_STATUS_JABDET_MASK        0x0002U
#define LAN8651_PHY_BASIC_STATUS_JABDET_SHIFT       1U
#define LAN8651_PHY_BASIC_STATUS_EXTCAPA_MASK       0x0001U
#define LAN8651_PHY_BASIC_STATUS_EXTCAPA_SHIFT      0U

/*================================================================================================*/
/*                             PHY ID REGISTER BITFIELDS                                         */
/*================================================================================================*/
/* PHY_ID1 Register (0x0000FF02) - PHY Identifier Register 1 */
#define LAN8651_PHY_ID1_OUI_2_9_MASK        0xFF00U
#define LAN8651_PHY_ID1_OUI_2_9_SHIFT       8U
#define LAN8651_PHY_ID1_OUI_10_17_MASK      0x00FFU
#define LAN8651_PHY_ID1_OUI_10_17_SHIFT     0U

/* PHY_ID2 Register (0x0000FF03) - PHY Identifier Register 2 */
#define LAN8651_PHY_ID2_OUI_18_23_MASK      0xFC00U
#define LAN8651_PHY_ID2_OUI_18_23_SHIFT     10U
#define LAN8651_PHY_ID2_MODEL_5_4_MASK      0x0300U
#define LAN8651_PHY_ID2_MODEL_5_4_SHIFT     8U
#define LAN8651_PHY_ID2_MODEL_3_0_MASK      0x00F0U
#define LAN8651_PHY_ID2_MODEL_3_0_SHIFT     4U
#define LAN8651_PHY_ID2_REV_MASK            0x000FU
#define LAN8651_PHY_ID2_REV_SHIFT           0U

/*================================================================================================*/
/*                                MMD REGISTER BITFIELDS                                         */
/*================================================================================================*/
/* MMDCTRL Register (0x0000FF0D) - MMD Access Control Register */
#define LAN8651_PHY_MMDCTRL_FNCTN_MASK      0xC000U
#define LAN8651_PHY_MMDCTRL_FNCTN_SHIFT     14U
#define LAN8651_PHY_MMDCTRL_DEVAD_MASK      0x001FU
#define LAN8651_PHY_MMDCTRL_DEVAD_SHIFT     0U

/* MMDAD Register (0x0000FF0E) - MMD Access Address/Data Register */
#define LAN8651_PHY_MMDAD_ADR_DATA_15_8_MASK    0xFF00U
#define LAN8651_PHY_MMDAD_ADR_DATA_15_8_SHIFT   8U
#define LAN8651_PHY_MMDAD_ADR_DATA_7_0_MASK     0x00FFU
#define LAN8651_PHY_MMDAD_ADR_DATA_7_0_SHIFT    0U

/*================================================================================================*/
/*                            MMS_1: MAC REGISTER ADDRESSES                                      */
/*================================================================================================*/
/* MMS_1: MAC Registers (32-bit) - Memory Map Selector = 0x0001 */
#define LAN8651_MAC_NCR                     0x00010000U     /* MAC Network Control Register */
#define LAN8651_MAC_NCFGR                   0x00010004U     /* MAC Network Configuration Register */
#define LAN8651_MAC_NSR                     0x00010008U     /* MAC Network Status Register */
#define LAN8651_MAC_TSR                     0x00010014U     /* MAC Transmit Status Register */
#define LAN8651_MAC_RBQB                    0x00010018U     /* MAC Receive Buffer Queue Base Address */
#define LAN8651_MAC_TBQB                    0x0001001CU     /* MAC Transmit Buffer Queue Base Address */

/* MAC Address Registers */
#define LAN8651_MAC_SAB1                    0x00010020U     /* MAC Specific Address 1 Bottom [31:0] */
#define LAN8651_MAC_SAT1                    0x00010024U     /* MAC Specific Address 1 Top [47:32] */
#define LAN8651_MAC_SAB2                    0x00010028U     /* MAC Specific Address 2 Bottom [31:0] */
#define LAN8651_MAC_SAT2                    0x0001002CU     /* MAC Specific Address 2 Top [47:32] */

/* TSU (Time Synchronization Unit) Registers */
#define LAN8651_MAC_TI                      0x00010070U     /* TSU Timer Increment */
#define LAN8651_MAC_TISUBN                  0x00010071U     /* TSU Timer Increment Sub-nanoseconds */
#define LAN8651_MAC_TA                      0x00010072U     /* TSU Timer Adjust */
#define LAN8651_MAC_TSH                     0x00010074U     /* TSU Seconds High [47:32] */
#define LAN8651_MAC_TSL                     0x00010078U     /* TSU Seconds Low [31:0] */
#define LAN8651_MAC_TN                      0x0001007CU     /* TSU Nanoseconds [29:0] */

/* TSU Event Timestamp Capture Registers */
#define LAN8651_MAC_ETSTSH                  0x00010080U     /* TSU Event Timestamp Seconds High */
#define LAN8651_MAC_ETSTSL                  0x00010084U     /* TSU Event Timestamp Seconds Low */
#define LAN8651_MAC_ETTSN                   0x00010088U     /* TSU Event Timestamp Nanoseconds */

/* Hash Table Registers */
#define LAN8651_MAC_HRB                     0x00010090U     /* MAC Hash Register Bottom */
#define LAN8651_MAC_HRT                     0x00010094U     /* MAC Hash Register Top */

/*================================================================================================*/
/*                         MMS_3: PHY PMA/PMD REGISTER ADDRESSES                                */
/*================================================================================================*/
/* MMS_3: PHY PMA/PMD Registers (16-bit) - Memory Map Selector = 0x0003 */
/* These registers contain Cable Fault Diagnostics (CFD) information */
#define LAN8651_PMD_CONTROL                 0x00030001U     /* PMA/PMD Control Register */
#define LAN8651_PMD_STATUS                  0x00030002U     /* PMA/PMD Status Register */

/*================================================================================================*/
/*                      MMS_3: PHY PMA/PMD REGISTER BITFIELDS                                    */
/*================================================================================================*/

/* PMD_CONTROL Register (0x00030001) - PMA/PMD Control Register */
#define LAN8651_PMD_CONTROL_CFD_START_MASK  0x0001U         /* Cable Fault Diagnostics Start */
#define LAN8651_PMD_CONTROL_CFD_START_SHIFT 0U
#define LAN8651_PMD_CONTROL_CFD_EN_MASK     0x0002U         /* Cable Fault Diagnostics Enable */
#define LAN8651_PMD_CONTROL_CFD_EN_SHIFT    1U

/* PMD_STATUS Register (0x00030002) - PMA/PMD Status Register */
#define LAN8651_PMD_STATUS_LINK_MASK        0x0004U         /* Link Status */
#define LAN8651_PMD_STATUS_LINK_SHIFT       2U
#define LAN8651_PMD_STATUS_FAULT_MASK       0x0008U         /* Fault Detected */
#define LAN8651_PMD_STATUS_FAULT_SHIFT      3U
#define LAN8651_PMD_STATUS_FAULT_TYPE_MASK  0x00F0U         /* Fault Type [7:4] */
#define LAN8651_PMD_STATUS_FAULT_TYPE_SHIFT 4U
#define LAN8651_PMD_STATUS_CFD_DONE_MASK    0x0100U         /* CFD Test Complete */
#define LAN8651_PMD_STATUS_CFD_DONE_SHIFT   8U

/*================================================================================================*/
/*                         MMS_1: MAC REGISTER BITFIELDS                                     */
/*================================================================================================*/

/* MAC_NCR Register (0x00010000) - MAC Network Control Register */
#define LAN8651_MAC_NCR_TXEN_MASK           0x00000004U     /* Transmit Enable */
#define LAN8651_MAC_NCR_TXEN_SHIFT          2U
#define LAN8651_MAC_NCR_RXEN_MASK           0x00000008U     /* Receive Enable */
#define LAN8651_MAC_NCR_RXEN_SHIFT          3U
#define LAN8651_MAC_NCR_TSUENA_MASK         0x00000020U     /* TSU Enable */
#define LAN8651_MAC_NCR_TSUENA_SHIFT        5U

/* MAC_NCFGR Register (0x00010004) - MAC Network Configuration Register */
#define LAN8651_MAC_NCFGR_SPD_MASK          0x00000003U     /* Speed Selection */
#define LAN8651_MAC_NCFGR_SPD_SHIFT         0U
#define LAN8651_MAC_NCFGR_FD_MASK           0x00000004U     /* Full Duplex */
#define LAN8651_MAC_NCFGR_FD_SHIFT          2U
#define LAN8651_MAC_NCFGR_DIS_CP_MASK       0x00000010U     /* Disable Copy of Pause Frames */
#define LAN8651_MAC_NCFGR_DIS_CP_SHIFT      4U

/* MAC_NSR Register (0x00010008) - MAC Network Status Register */
#define LAN8651_MAC_NSR_MDIO_MASK           0x00000001U     /* MDIO IN Pin Status */
#define LAN8651_MAC_NSR_MDIO_SHIFT          0U

/* MAC_TSR Register (0x00010014) - MAC Transmit Status Register */
#define LAN8651_MAC_TSR_UBR_MASK            0x00000001U     /* Used Bit Read */
#define LAN8651_MAC_TSR_UBR_SHIFT           0U
#define LAN8651_MAC_TSR_COL_MASK            0x00000002U     /* Collision Occurred */
#define LAN8651_MAC_TSR_COL_SHIFT           1U
#define LAN8651_MAC_TSR_RLE_MASK            0x00000004U     /* Retry Limit Exceeded */
#define LAN8651_MAC_TSR_RLE_SHIFT           2U

/* MAC_TI Register (0x00010070) - TSU Timer Increment */
#define LAN8651_MAC_TI_CNS_MASK             0xFFFFFFFFU     /* Timer Increment in Nanoseconds */
#define LAN8651_MAC_TI_CNS_SHIFT            0U

/* MAC_TSH Register (0x00010074) - TSU Seconds High [47:32] */
#define LAN8651_MAC_TSH_SEC_47_32_MASK      0x0000FFFFU     /* Seconds [47:32] */
#define LAN8651_MAC_TSH_SEC_47_32_SHIFT     0U

/* MAC_TSL Register (0x00010078) - TSU Seconds Low [31:0] */
#define LAN8651_MAC_TSL_SEC_31_0_MASK       0xFFFFFFFFU     /* Seconds [31:0] */
#define LAN8651_MAC_TSL_SEC_31_0_SHIFT      0U

/* MAC_TN Register (0x0001007C) - TSU Nanoseconds [29:0] */
#define LAN8651_MAC_TN_NSEC_MASK            0x3FFFFFFFU     /* Nanoseconds [29:0] */
#define LAN8651_MAC_TN_NSEC_SHIFT           0U

/*================================================================================================*/
/*                                  UTILITY MACROS                                               */
/*================================================================================================*/

/**
 * @brief Extract a bitfield from a register value
 * @param reg Register value
 * @param mask Bitfield mask
 * @param shift Bitfield shift position
 * @return Extracted bitfield value
 */
#define LAN8651_GET_BITFIELD(reg, mask, shift)     (((reg) & (mask)) >> (shift))

/**
 * @brief Set a bitfield in a register value
 * @param reg Register value (will be modified)
 * @param mask Bitfield mask
 * @param shift Bitfield shift position  
 * @param val Value to set in the bitfield
 */
#define LAN8651_SET_BITFIELD(reg, mask, shift, val) \
    ((reg) = ((reg) & ~(mask)) | (((val) << (shift)) & (mask)))

/**
 * @brief Create a bitfield value ready to OR into register
 * @param mask Bitfield mask
 * @param shift Bitfield shift position
 * @param val Value to encode
 * @return Shifted and masked value
 */
#define LAN8651_BITFIELD_VAL(mask, shift, val)    (((val) << (shift)) & (mask))

/**
 * @brief Check if a bitfield bit is set
 * @param reg Register value
 * @param mask Single bit mask
 * @return Non-zero if bit is set, zero if clear
 */
#define LAN8651_IS_BIT_SET(reg, mask)             ((reg) & (mask))

/**
 * @brief Clear a bitfield bit
 * @param reg Register value (will be modified)
 * @param mask Single bit mask
 */  
#define LAN8651_CLEAR_BIT(reg, mask)              ((reg) &= ~(mask))

/**
 * @brief Set a bitfield bit
 * @param reg Register value (will be modified)
 * @param mask Single bit mask
 */
#define LAN8651_SET_BIT(reg, mask)                ((reg) |= (mask))

/*================================================================================================*/
/*                                REGISTER VALUE CONSTANTS                                       */
/*================================================================================================*/

/* Expected LAN8651 Device Identification Values */
#define LAN8651_EXPECTED_OA_ID              0x00000011U     /* Version 1.1 */
#define LAN8651_EXPECTED_PHY_ID1            0x0007U         /* Microchip OUI */
#define LAN8651_EXPECTED_PHY_ID2_MASK       0xC0F0U         /* LAN865x Model */

/* Common Configuration Values */
#define LAN8651_CONFIG0_ENABLE_SYNC         LAN8651_BITFIELD_VAL(LAN8651_OA_CONFIG0_SYNC_MASK, LAN8651_OA_CONFIG0_SYNC_SHIFT, 1)
#define LAN8651_CONFIG0_ENABLE_TXFCSVE      LAN8651_BITFIELD_VAL(LAN8651_OA_CONFIG0_TXFCSVE_MASK, LAN8651_OA_CONFIG0_TXFCSVE_SHIFT, 1)
#define LAN8651_CONFIG0_ENABLE_PROTE        LAN8651_BITFIELD_VAL(LAN8651_OA_CONFIG0_PROTE_MASK, LAN8651_OA_CONFIG0_PROTE_SHIFT, 1)

/* PHY Control Values */
#define LAN8651_PHY_RESET                   LAN8651_BITFIELD_VAL(LAN8651_PHY_BASIC_CONTROL_SW_RESET_MASK, LAN8651_PHY_BASIC_CONTROL_SW_RESET_SHIFT, 1)
#define LAN8651_PHY_POWER_DOWN              LAN8651_BITFIELD_VAL(LAN8651_PHY_BASIC_CONTROL_PD_MASK, LAN8651_PHY_BASIC_CONTROL_PD_SHIFT, 1)
#define LAN8651_PHY_ENABLE_AUTONEG          LAN8651_BITFIELD_VAL(LAN8651_PHY_BASIC_CONTROL_AUTONEGEN_MASK, LAN8651_PHY_BASIC_CONTROL_AUTONEGEN_SHIFT, 1)

/*================================================================================================*/
/*                                    EXAMPLE USAGE                                              */
/*================================================================================================*/

#if 0  /* Example code - not compiled */

/* Reading Device ID */
uint32_t oa_id = lan_read(LAN8651_OA_ID);
uint8_t major_ver = LAN8651_GET_BITFIELD(oa_id, LAN8651_OA_ID_MAJVER_MASK, LAN8651_OA_ID_MAJVER_SHIFT);
uint8_t minor_ver = LAN8651_GET_BITFIELD(oa_id, LAN8651_OA_ID_MINVER_MASK, LAN8651_OA_ID_MINVER_SHIFT);

/* Configuring Protocol Settings */
uint32_t config0 = lan_read(LAN8651_OA_CONFIG0);
LAN8651_SET_BITFIELD(config0, LAN8651_OA_CONFIG0_SYNC_MASK, LAN8651_OA_CONFIG0_SYNC_SHIFT, 1);
LAN8651_SET_BITFIELD(config0, LAN8651_OA_CONFIG0_PROTE_MASK, LAN8651_OA_CONFIG0_PROTE_SHIFT, 1);
lan_write(LAN8651_OA_CONFIG0, config0);

/* Checking Link Status */
uint16_t status = (uint16_t)lan_read(LAN8651_PHY_BASIC_STATUS);
if (LAN8651_IS_BIT_SET(status, LAN8651_PHY_BASIC_STATUS_LNKSTS_MASK)) {
    /* Link is up */
}

/* Software Reset */
uint32_t reset_reg = lan_read(LAN8651_OA_RESET);
LAN8651_SET_BIT(reset_reg, LAN8651_OA_RESET_SWRESET_MASK);
lan_write(LAN8651_OA_RESET, reset_reg);

#endif /* Example code */

#endif /* LAN8651_REGS_H */