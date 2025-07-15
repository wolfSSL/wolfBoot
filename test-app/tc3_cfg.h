/* tc3_cfg.h */

#ifndef TC3_CFG_H
#define TC3_CFG_H

#if defined(WOLFBOOT_AURIX_TC3XX_HSM)
#define TC3_CFG_HAVE_ARM
#else
#define TC3_CFG_HAVE_TRICORE
#endif

#define TC3_CFG_HAVE_BOARD
#define TC3_BOARD_TC375LITEKIT 1

#endif /* TC3_CFG_H */
