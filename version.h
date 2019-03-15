//====================================================================================
// Revision History :
//---------------------------------------------------------------
// 2018.11.16 by Hank.Peng
//	 First version
// 
//---------------------------------------------------------------
// Copyright (c) Advantech Co., Ltd. All Rights Reserved
//====================================================================================
#ifndef _WASCADA_HANDLER_VERSION_H_
#define _WASCADA_HANDLER_VERSION_H_

#include "WASCADAHandlerVer.h"

#if !defined(SVN_REVISION)
#include "../../Include/svnversion.h"
#endif

#define VER_MAJOR	MAIN_VERSION
#define VER_MINOR	SUB_VERSION
#define VER_BUILD	BUILD_VERSION
#define VER_FIX		SVN_REVISION

#define U32VER		(VER_MAJOR << 24 | VER_MINOR << 16 | VER_BUILD)

#define CREATE_XVER(maj,min,build,fix) 		maj ##, ## min ##, ## build ##, ## fix

#endif /* _WASCADA_HANDLER_VERSION_H_ */
