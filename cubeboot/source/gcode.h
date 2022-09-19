/*-------------------------------------------------------------

gcode.h -- GCODE subsystem

Copyright (C) 2004
Michael Wiedenbauer (shagkur)
Dave Murphy (WinterMute)

This software is provided 'as-is', without any express or implied
warranty.  In no event will the authors be held liable for any
damages arising from the use of this software.

Permission is granted to anyone to use this software for any
purpose, including commercial applications, and to alter it and
redistribute it freely, subject to the following restrictions:

1.	The origin of this software must not be misrepresented; you
must not claim that you wrote the original software. If you use
this software in a product, an acknowledgment in the product
documentation would be appreciated but is not required.

2.	Altered source versions must be plainly marked as such, and
must not be misrepresented as being the original software.

3.	This notice may not be removed or altered from any source
distribution.

-------------------------------------------------------------*/


#ifndef __GCODE_H__
#define __GCODE_H__

/*! 
 * \file gcode.h 
 * \brief GCODE subsystem
 *
 */ 

#include <gctypes.h>
#include <ogc/lwp_queue.h>
#include <ogc/disc_io.h>

/*! 
 * \addtogroup gcode_statecodes GCODE state codes
 * @{
 */

#define  GCODE_STATE_FATAL_ERROR			-1 
#define  GCODE_STATE_END					0 
#define  GCODE_STATE_BUSY					1 
#define  GCODE_STATE_WAITING				2 
#define  GCODE_STATE_COVER_CLOSED			3 
#define  GCODE_STATE_NO_DISK				4 
#define  GCODE_STATE_COVER_OPEN			5 
#define  GCODE_STATE_WRONG_DISK			6 
#define  GCODE_STATE_MOTOR_STOPPED		7 
#define  GCODE_STATE_PAUSING				8 
#define  GCODE_STATE_IGNORED				9 
#define  GCODE_STATE_CANCELED				10 
#define  GCODE_STATE_RETRY				11 

#define  GCODE_ERROR_OK					0 
#define  GCODE_ERROR_FATAL				-1 
#define  GCODE_ERROR_IGNORED				-2 
#define  GCODE_ERROR_CANCELED				-3 
#define  GCODE_ERROR_COVER_CLOSED			-4 

/*!
 * @}
 */


/*! 
 * \addtogroup gcode_resetmode GCODE reset modes
 * @{
 */

#define GCODE_RESETHARD					0			/*!< Performs a hard reset. Complete new boot of FW. */
#define GCODE_RESETSOFT					1			/*!< Performs a soft reset. FW restart and drive spinup */
#define GCODE_RESETNONE					2			/*!< Only initiate DI registers */

/*!
 * @}
 */


/*! 
 * \addtogroup gcode_motorctrlmode GCODE motor control modes
 * @{
 */

#define GCODE_SPINMOTOR_DOWN				0x00000000	/*!< Stop GCODE drive */
#define GCODE_SPINMOTOR_UP				0x00000100  /*!< Start GCODE drive */
#define GCODE_SPINMOTOR_ACCEPT			0x00004000	/*!< Force GCODE to accept the disk */
#define GCODE_SPINMOTOR_CHECKDISK			0x00008000	/*!< Force GCODE to perform a disc check */

/*!
 * @}
 */


#ifdef __cplusplus
   extern "C" {
#endif /* __cplusplus */


/*!
 * \typedef struct _gcodediskid gcodediskid
 * \brief forward typedef for struct _gcodediskid
 */
typedef struct _gcodediskid gcodediskid;

/*!
 * \typedef struct _gcodediskid gcodediskid
 *
 *        This structure holds the game vendors copyright informations.<br>
 *        Additionally it holds certain parameters for audiocontrol and<br>
 *        multidisc support.
 *
 * \param gamename[4] vendors game key
 * \param company[2] vendors company key
 * \param disknum number of disc when multidisc support is used.
 * \param gamever version of game
 * \param streaming flag to control audio streaming
 * \param streambufsize size of buffer used for audio streaming
 * \param pad[22] padding 
 */
struct _gcodediskid {
	s8 gamename[4];
	s8 company[2];
	u8 disknum;
	u8 gamever;
	u8 streaming;
	u8 streambufsize;
	u8 pad[22];
};

/*!
 * \typedef struct _gcodecmdblk gcodecmdblk
 * \brief forward typedef for struct _gcodecmdblk
 */
typedef struct _gcodecmdblk gcodecmdblk;


/*!
 * \typedef void (*gcodecbcallback)(s32 result,gcodecmdblk *block)
 * \brief function pointer typedef for the user's operations callback
 */
typedef void (*gcodecbcallback)(s32 result,gcodecmdblk *block);


/*!
 * \typedef struct _gcodecmdblk gcodecmdblk
 *
 *        This structure is used internally to control the requested operation.
 */
struct _gcodecmdblk {
	lwp_node node;
	u32 cmd;
	s32 state;
	s64 offset;
	u32 len;
	void *buf;
	u32 currtxsize;
	u32 txdsize;
	gcodediskid *id;
	gcodecbcallback cb;
	void *usrdata;
};


/*!
 * \typedef struct _gcodedrvinfo gcodedrvinfo
 * \brief forward typedef for struct _gcodedrvinfo
 */
typedef struct _gcodedrvinfo gcodedrvinfo;


/*!
 * \typedef struct _gcodedrvinfo gcodedrvinfo
 *
 *        This structure structure holds the drive version infromation.<br>
 *		  Use GCODE_Inquiry() to retrieve this information.
 *
 * \param rev_leve revision level
 * \param dev_code device code
 * \param rel_date release date
 * \param pad[24] padding
 */
struct _gcodedrvinfo {
	u16 rev_level;
	u16 dev_code;
	u32 rel_date;
	u8  pad[24];
};


/*!
 * \typedef struct _gcodefileinfo gcodefileinfo
 * \brief forward typedef for struct _gcodefileinfo
 */
typedef struct _gcodefileinfo gcodefileinfo;


/*!
 * \typedef void (*gcodecallback)(s32 result,gcodefileinfo *info)
 * \brief function pointer typedef for the user's GCODE operation callback
 *
 * \param[in] result error code of last operation
 * \param[in] info pointer to user's file info strucutre
 */
typedef void (*gcodecallback)(s32 result,gcodefileinfo *info);


/*!
 * \typedef struct _gcodefileinfo gcodefileinfo
 *
 *        This structure is used internally to control the requested file operation.
 */
struct _gcodefileinfo {
	gcodecmdblk block;
	u32 addr;
	u32 len;
	gcodecallback cb;
};


/*! 
 * \fn void GCODE_Init(void)
 * \brief Initializes the GCODE subsystem
 *
 *        You must call this function before calling any other GCODE function
 *
 * \return none
 */
void GCODE_Init(void);
void GCODE_Pause(void);


/*! 
 * \fn void GCODE_Reset(u32 reset_mode)
 * \brief Performs a reset of the drive and FW respectively.
 *
 * \param[in] reset_mode \ref gcode_resetmode "type" of reset
 *
 * \return none
 */
void GCODE_Reset(u32 reset_mode);


/*! 
 * \fn s32 GCODE_Mount(void)
 * \brief Mounts the GCODE drive.
 *
 *        This is a synchronous version of GCODE_MountAsync().
 *
 * \return none
 */
s32 GCODE_Mount(void);
s32 GCODE_GetDriveStatus(void);


/*! 
 * \fn s32 GCODE_MountAsync(gcodecmdblk *block,gcodecbcallback cb)
 * \brief Mounts the GCODE drive.
 *
 *        You <b>must</b> call this function in order to access the GCODE.
 *
 *        Following tasks are performed:
 *      - Issue a hard reset to the drive.
 *      - Turn on drive's debug mode.
 *      - Patch drive's FW.
 *      - Enable extensions.
 *      - Read disc ID
 *
 *        The patch code and procedure was taken from the gc-linux GCODE device driver.
 *
 * \param[in] block pointer to a gcodecmdblk structure used to process the operation
 * \param[in] cb callback to be invoked upon completion of operation
 *
 * \return none
 */
s32 GCODE_MountAsync(gcodecmdblk *block,gcodecbcallback cb);


/*! 
 * \fn s32 GCODE_ControlDrive(gcodecmdblk *block,u32 cmd)
 * \brief Controls the drive's motor and behavior.
 *
 *        This is a synchronous version of GCODE_ControlDriveAsync().
 *
 * \param[in] block pointer to a gcodecmdblk structure used to process the operation
 * \param[in] cmd \ref gcode_motorctrlmode "command" to control the drive.
 *
 * \return none
 */
s32 GCODE_ControlDrive(gcodecmdblk *block,u32 cmd);


/*! 
 * \fn s32 GCODE_ControlDriveAsync(gcodecmdblk *block,u32 cmd,gcodecbcallback cb)
 * \brief Controls the drive's motor and behavior.
 *
 * \param[in] block pointer to a gcodecmdblk structure used to process the operation
 * \param[in] cmd \ref gcode_motorctrlmode "command" to control the drive.
 * \param[in] cb callback to be invoked upon completion of operation.
 *
 * \return none
 */
s32 GCODE_ControlDriveAsync(gcodecmdblk *block,u32 cmd,gcodecbcallback cb);


/*! 
 * \fn s32 GCODE_SetGCMOffset(gcodecmdblk *block,u32 offset)
 * \brief Sets the offset to the GCM. Used for multigame discs.
 *
 *        This is a synchronous version of GCODE_SetGCMOffsetAsync().
 *
 * \param[in] block pointer to a gcodecmdblk structure used to process the operation
 * \param[in] offset offset to the GCM on disc.
 *
 * \return \ref gcode_errorcodes "gcode error code"
 */
s32 GCODE_SetGCMOffset(gcodecmdblk *block,s64 offset);


/*! 
 * \fn s32 GCODE_SetGCMOffsetAsync(gcodecmdblk *block,u32 offset,gcodecbcallback cb)
 * \brief Sets the offset to the GCM. Used for multigame discs.
 *
 *        This is a synchronous version of GCODE_SetGCMOffsetAsync().
 *
 * \param[in] block pointer to a gcodecmdblk structure used to process the operation
 * \param[in] offset offset to the GCM on disc.
 * \param[in] cb callback to be invoked upon completion of operation.
 *
 * \return \ref gcode_errorcodes "gcode error code"
 */
s32 GCODE_SetGCMOffsetAsync(gcodecmdblk *block,s64 offset,gcodecbcallback cb);

s32 GCODE_GcodeRead(gcodecmdblk *block,void *buf,u32 len,u32 offset);
s32 GCODE_GcodeReadAsync(gcodecmdblk *block,void *buf,u32 len,u32 offset,gcodecbcallback cb);
s32 GCODE_GetCmdBlockStatus(gcodecmdblk *block);
s32 GCODE_SpinUpDrive(gcodecmdblk *block);
s32 GCODE_SpinUpDriveAsync(gcodecmdblk *block,gcodecbcallback cb);
s32 GCODE_Inquiry(gcodecmdblk *block,gcodedrvinfo *info);
s32 GCODE_InquiryAsync(gcodecmdblk *block,gcodedrvinfo *info,gcodecbcallback cb);
s32 GCODE_StopMotor(gcodecmdblk *block);
s32 GCODE_StopMotorAsync(gcodecmdblk *block,gcodecbcallback cb);
s32 GCODE_ReadPrio(gcodecmdblk *block,void *buf,u32 len,s64 offset,s32 prio);
s32 GCODE_ReadAbsAsyncPrio(gcodecmdblk *block,void *buf,u32 len,s64 offset,gcodecbcallback cb,s32 prio);
s32 GCODE_ReadAbsAsyncForBS(gcodecmdblk *block,void *buf,u32 len,s64 offset,gcodecbcallback cb);
s32 GCODE_SeekPrio(gcodecmdblk *block,s64 offset,s32 prio);
s32 GCODE_SeekAbsAsyncPrio(gcodecmdblk *block,s64 offset,gcodecbcallback cb,s32 prio);
s32 GCODE_CancelAllAsync(gcodecbcallback cb);
s32 GCODE_StopStreamAtEndAsync(gcodecmdblk *block,gcodecbcallback cb);
s32 GCODE_StopStreamAtEnd(gcodecmdblk *block);
s32 GCODE_ReadDiskID(gcodecmdblk *block,gcodediskid *id,gcodecbcallback cb);
u32 GCODE_SetAutoInvalidation(u32 auto_inv);
gcodediskid* GCODE_GetCurrentDiskID(void);
gcodedrvinfo* GCODE_GetDriveInfo(void);

#define GCODE_SetUserData(block, data) ((block)->usrdata = (data))
#define GCODE_GetUserData(block)       ((block)->usrdata)

#define DEVICE_TYPE_GAMECUBE_GCODE		(('G'<<24)|('D'<<16)|('V'<<8)|'D')

extern const DISC_INTERFACE __io_gcode;

#ifdef __cplusplus
   }
#endif /* __cplusplus */

#endif
