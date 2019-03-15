/****************************************************************************/
/* Copyright(C) : Advantech Technologies, Inc.								*/
/* Create Date  : 2016/07/14 by Scott Chang									*/
/* Modified Date: 2016/07/14 by Scott Chang									*/
/* Abstract     : WA-SCADA Handler to parse json string in c:/test.txt file	*/
/*                and report to server.										*/	
/* Reference    : None														*/
/****************************************************************************/

#include "stdafx.h"
#include "susiaccess_handler_api.h"
#include "DeviceMessageGenerate.h"
#include "IoTMessageGenerate.h"
#include "FlatToIPSO.h"
#include <Log.h>
#include <stdio.h>
#include "cJSON.h"
#include "HandlerKernel.h"

#include "util_path.h"
#include "ReadINI.h"
#include "WebAccessData.h"
#include "version.h"
#include "description.h"

//-----------------------------------------------------------------------------
// Logger defines:
//-----------------------------------------------------------------------------
#define WASCANDAHANDLER_LOG_ENABLE
//#define DEF_WASCANDAHANDLER_LOG_MODE    (LOG_MODE_NULL_OUT)
//#define DEF_WASCANDAHANDLER_LOG_MODE    (LOG_MODE_FILE_OUT)
#define DEF_WASCANDAHANDLER_LOG_MODE    (LOG_MODE_CONSOLE_OUT|LOG_MODE_FILE_OUT)

LOGHANDLE g_wascadahandlerlog = NULL;

#ifdef WASCANDAHANDLER_LOG_ENABLE
#define WASCADAHLog(level, fmt, ...)  do { if (g_wascadahandlerlog != NULL)   \
	WriteLog(g_wascadahandlerlog, DEF_WASCANDAHANDLER_LOG_MODE, level, fmt, ##__VA_ARGS__); } while(0)
#else
#define WASCADAHLog(level, fmt, ...)
#endif

//-----------------------------------------------------------------------------
// Types and defines:
//-----------------------------------------------------------------------------
#define cagent_request_custom  2102    /*define the request ID for V3.0, not used on V3.1 or later*/
#define cagent_custom_action   31002   /*define the action ID for V3.0, not used on V3.1 or later*/

#define MAX_VERSION_LEN        32
#define MAX_DESCRIPTION_LEN    200
#define MAX_JSON_BUF_LEN       102400
#define MAX_NAME_BUF_LEN       100
#define MAX_NAMELIST_NUM       300
#define MAX_SERVER_LEN         256
#define MAX_IP_ADDR_LEN        32
#define MAX_PORT_ADDR_LEN      10
#define MAX_GENERAL_BUF_LEN    50

#define TAG_TYPE_ANALOG        1
#define TAG_TYPE_DIGITAL       2
#define TAG_TYPE_TEXT          3

#define TAG_FULL_PATH_REPORT

//char strHandlerName[MAX_TOPIC_LEN]; /*declare the handler name*/
char *strHandlerName = NULL;
char *strPluginVersion = NULL;
char *strPluginDescription = NULL;

MSG_CLASSIFY_T *g_Capability = NULL; /*the global message structure to describe the sensor data as the handelr capability*/
static HandlerSendEventCbf g_sendeventcbf = NULL;							// Client Send information (in JSON format) to Cloud Server

//-----------------------------------------------------------------------------
// Internal Prototypes:
//-----------------------------------------------------------------------------
//
typedef struct Handler_context_t
{
   void* threadHandler;  // thread handle
   void* threadHandlerGetTagDetail;
   int interval;		 // time interval for file read
   bool isThreadRunning; //thread running flag
   bool isThreadRunningGetTagDetail; //thread running flag
} Handler_context, *pHandler_context;


typedef struct TAG_context_t
{
	int  TagNumber;
	char ProjectName[MAX_NAME_BUF_LEN];
	char NodeName[MAX_NAME_BUF_LEN];
	char PortNumber;                          // Range from 1 ~ 60
	char DeviceName[MAX_NAME_BUF_LEN];
	char TagName[MAX_NAME_BUF_LEN];
	int  Readonly;
	int  TagType;
	double TagValue;
	char TagValueText[MAX_NAME_BUF_LEN];
	int  TagQuality;                          // Normal: 0
	bool TagAvailable;                        // Tag detail available: true, unavailable: false
} TAG_context, *pTAG_context;


typedef struct INI_context_t
{
	bool ResyncCapability;
	char Server[MAX_SERVER_LEN];
	char Port[MAX_PORT_ADDR_LEN];
	char DataBase[MAX_GENERAL_BUF_LEN];
	char Authorization[MAX_GENERAL_BUF_LEN];
	int RetrieveInterval_ms;
	bool RetrieveSpecificTags;
	int PollingTagInterval_sec;
	bool PollingTagNotification;
	int numberOfTags;
	int numberOfUnavailableTags;
	pTAG_context pTags;
} INI_context, *pINI_context;
INI_context INI_data;


typedef struct Project_context_t {
	int  ID;
	char Name[MAX_NAME_BUF_LEN];
	char Description[MAX_DESCRIPTION_LEN];
	char IP[MAX_IP_ADDR_LEN];
	int  Port;
	int  Timeout;
	char AccessSecurityCode[100];
	int  HTTPPort;
} Project_context, *pProject_context;


typedef struct Node_context_t {
	int  ProjectId;
	int  NodeId;
	char NodeName[MAX_NAME_BUF_LEN];
	char Description[MAX_DESCRIPTION_LEN];
	char Address[MAX_IP_ADDR_LEN];
	int  Port1;
	int  Port2;
	int  Timeout;
} Node_context, *pNode_context;


typedef struct Port_context_t {
	char InterfaceName[MAX_NAME_BUF_LEN];
	int  ComportNbr;
	char Description[MAX_DESCRIPTION_LEN];
	int  BaudRate;
	int  DataBit;
	int  StopBit;
	int  Parity;
	int  ScanTime;
	int  TimeOut;
	int  RetryCount;
	int  AutoRecoverTime;
	char OPCServer[MAX_GENERAL_BUF_LEN];
	char OPCServerType[MAX_GENERAL_BUF_LEN];
} Port_context, *pPort_context;


typedef struct Connection_context_t {
	char IPAddress[MAX_IP_ADDR_LEN];
	char PortNumber[MAX_GENERAL_BUF_LEN];
	char DeviceAddress[MAX_GENERAL_BUF_LEN];
} Connection, *pConnection;


typedef struct Device_context_t {
	char DeviceName[MAX_NAME_BUF_LEN];
	int  PortNumber;
	char Description[MAX_DESCRIPTION_LEN];
	int  UnitNumber;
	char DeviceType[MAX_GENERAL_BUF_LEN];
	Connection Primary;
	Connection Secondary;
} Device_context, *pDevice_context;


typedef struct EventNotify_context_t
{
	char Id[128];
	char subtype[128];
	char handler[128];
	char extMsg[512];
	int severity;
	char msg[256];
} EventNotify_Context, *EventNotify_ContextPtr;


//char *strPluginName = NULL;
bool bFind = false; //Find INI file
char jsonProjectList[MAX_JSON_BUF_LEN];
char jsonNodeList[MAX_JSON_BUF_LEN];
char jsonPortList[MAX_JSON_BUF_LEN];
char jsonDeviceList[MAX_JSON_BUF_LEN];
char jsonTagList[MAX_JSON_BUF_LEN];
char jsonTagValue[MAX_JSON_BUF_LEN];

char projectNameList[MAX_NAMELIST_NUM][MAX_NAME_BUF_LEN];
char nodeNameList[MAX_NAMELIST_NUM][MAX_NAME_BUF_LEN];
char portNameList[MAX_NAMELIST_NUM][MAX_NAME_BUF_LEN];
int  portNumberList[MAX_NAMELIST_NUM];
char deviceNameList[MAX_NAMELIST_NUM][MAX_NAME_BUF_LEN];
char tagNameList[MAX_NAMELIST_NUM][MAX_NAME_BUF_LEN];


//-----------------------------------------------------------------------------
// Variables
//-----------------------------------------------------------------------------
static Handler_info  g_HandlerInfo; //global Handler info structure
static Handler_context g_HandlerContex;
static HANDLER_THREAD_STATUS g_status = handler_status_no_init; // global status flag.
static HandlerSendCbf  g_sendcbf = NULL;						// Client Send information (in JSON format) to Cloud Server


//-----------------------------------------------------------------------------
// Function:
//-----------------------------------------------------------------------------
MSG_CLASSIFY_T * CreateCapability();
void Handler_Uninitialize();
bool read_INI();
bool ParseProjectList(const char* jsonList);
bool ParseNodeList(const char* jsonList);
bool ParsePortList(const char* jsonList);
bool ParseDeviceList(const char* jsonList);
bool ParseTagList(const char* jsonList);
bool ParseTagValue(const char* jsonTagValue, int index);
bool ParseTagValueText(const char* jsonTagValueText, int index);
bool ParseTagDetail(const char* jsonTagDetail, int index, bool getTypeOnly);
bool UpdateProjectList(MSG_CLASSIFY_T *parentGroup);
bool UpdateNodeList(MSG_CLASSIFY_T *parentGroup, char *projectName);
bool UpdatePortList(MSG_CLASSIFY_T *parentGroup, char *projectName, char *nodeName);
bool UpdateDeviceList(MSG_CLASSIFY_T *parentGroup, char *projectName, char *nodeName, int portNumber);
bool UpdateTagList(MSG_CLASSIFY_T *parentGroup, char *projectName, char *nodeName, int portNumber, char *deviceName);
bool UpdateTagValueByIndex(MSG_CLASSIFY_T *parentGroup, int index);
bool UpdateSpecificTagsDetail(MSG_CLASSIFY_T *parentGroup);
bool UpdateSpecificTags(MSG_CLASSIFY_T *parentGroup);
bool PrepareEvents(char* eventId, char* eventSubtype, char* eventMessage, char* eventDescription, int eventSeverity);
bool ReportEventNotify(EventNotify_ContextPtr pNotifyData);
void ReportTagUnavailableByIndex(MSG_CLASSIFY_T *parentGroup, int index);

#ifdef _MSC_VER
BOOL WINAPI DllMain(HINSTANCE module_handle, DWORD reason_for_call, LPVOID reserved)
{
	if (reason_for_call == DLL_PROCESS_ATTACH) // Self-explanatory
	{
		DisableThreadLibraryCalls(module_handle); // Disable DllMain calls for DLL_THREAD_*
		if (reserved == NULL) // Dynamic load
		{
			// Initialize your stuff or whatever
			// Return FALSE if you don't want your module to be dynamically loaded
		}
		else // Static load
		{
			// Return FALSE if you don't want your module to be statically loaded
			return FALSE;
		}
	}

	if (reason_for_call == DLL_PROCESS_DETACH) // Self-explanatory
	{
		if (reserved == NULL) // Either loading the DLL has failed or FreeLibrary was called
		{
			// Cleanup
			Handler_Uninitialize();
		}
		else // Process is terminating
		{
			// Cleanup
			Handler_Uninitialize();
		}
	}
	return TRUE;
}
#else
__attribute__((constructor))
/**
 * initializer of the shared lib.
 */
static void Initializer(int argc, char** argv, char** envp)
{
    printf("DllInitializer\r\n");
}

__attribute__((destructor))
/** 
 * It is called when shared lib is being unloaded.
 * 
 */
static void Finalizer()
{
    printf("DllFinalizer\r\n");
	Handler_Uninitialize();
}
#endif

/*
bool ParseReceivedData(MSG_CLASSIFY_T *pGroup)
{
	// Data String: {"<tag>":<Number>, "<tag>":true, "<tag>":false, "<tag>":null, "<tag>":"<string>"}
	char* data = (char *)FileRead();
	if(!data) return false;
	if(strlen(data)<=0) return false;	
	return transfer_parse_json(data, pGroup);
}


const char* GetProjectList()
{
	strcpy(jsonProjectList, "{\"Result\":{\"Ret\":0,\"Total\":2},\"Projects\":[{\"ID\":1,\"Name\":\" Express\",\"Description\":\"Project Description\"},{\"ID\":2,\"Name\":\"WaScada\",\"Description\":\"Project Description\"}]}");
	return jsonProjectList;
}

const char* GetNodeList(char* projectName)
{
	strcpy(jsonNodeList, "{\"Result\":{\"Ret\":0,\"Total\":2},\"Nodes\":[{\"NodeName\":\"SCADA1\",\"Description\":\"SCADA Node 1 Description\"},{\"NodeName\":\"SCADA2\",\"Description\":\"SCADA Node 2 Description\"}]}");
	return jsonNodeList;
}

const char* GetPortList(char* nodeName)
{
	strcpy(jsonPortList, "{\"Result\":{\"Ret\":0,\"Total\":2},\"Ports\":[{\"InterfaceName\":\"TCP\",\"PortNumber\":2,\"Description\":\"TCP PORT\"},{\"InterfaceName\":\"TCP\",\"PortNumber\":1,\"Description\":\"TCP PORT\"}]}");
	return jsonPortList;
}

const char* GetDeviceList(char* portName)
{
	strcpy(jsonDeviceList, "{\"Result\":{\"Ret\":0,\"Total\":3},\"Devices\":[{\"DeviceName\":\"ADAM6017_5\",\"PortNumber\":2,\"Description\":\"ADAM-6017 8ch AI for WebAccess\",\"UnitNumber\":5},{\"DeviceName\":\"ADAM6018_6\",\"PortNumber\":2,\"Description\":\" ADAM-6018 AI\",\"UnitNumber\":6},{\"DeviceName\":\"ADAM6050_10\",\"PortNumber\":2,\"Description\":\"ADAM 6050 Module\",\"UnitNumber\":10}]}");
	return jsonDeviceList;
}

const char* GetTagList(char* deviceName)
{
	strcpy(jsonTagList, "{\"Result\":{\"Ret\":0,\"Total\":2},\"Tags\":[{\"ID\":\"1\",\"Name\":\"Tag1\"},{\"ID\":\"2\",\"Name\":\"Tag2\"}]}");
	return jsonTagList;
}

const char* GetTagValue(char* tagName)
{
	strcpy(jsonTagValue, "{\"Result\":{\"Ret\":0,\"Total\":1},\"Values\":[{\"Name\":\"Tag1\",\"Value\":222,\"Quality\":0}]}");
	return jsonTagValue;
}


const char* GetTagDetail(char* tagName)
{
	strcpy(jsonTagDetail, "{\"Result\":{\"Ret\":0,\"Total\":1},\"Values\":[{\"Name\":\"Tag1\",\"Value\":222,\"Quality\":0}]}");
	return jsonTagDetail;
}
*/


void InitProjectNameList()
{
	int row, col;
	for (row = 0; row < MAX_NAMELIST_NUM; row++)
	{
		for (col = 0; col < 30; col++)
		{
			projectNameList[row][col] = '\0';
		}
	}
}

void InitNodeNameList()
{
	int row, col;
	for (row = 0; row < MAX_NAMELIST_NUM; row++)
	{
		for (col = 0; col < 30; col++)
		{
			nodeNameList[row][col] = '\0';
		}
	}
}

void InitPortNameList()
{
	int row, col;
	for (row = 0; row < MAX_NAMELIST_NUM; row++)
	{
		for (col = 0; col < 30; col++)
		{
			portNameList[row][col] = '\0';
		}
	}
}

void InitDeviceNameList()
{
	int row, col;
	for (row = 0; row < MAX_NAMELIST_NUM; row++)
	{
		for (col = 0; col < 30; col++)
		{
			deviceNameList[row][col] = '\0';
		}
	}
}

void InitTagNameList()
{
	int row, col;
	for (row = 0; row < MAX_NAMELIST_NUM; row++)
	{
		for (col = 0; col < 30; col++)
		{
			tagNameList[row][col] = '\0';
		}
	}
}


void InitTags()
{
	for(int i=0; i<MAX_NAMELIST_NUM; i++)
	{
		// Initialize
		INI_data.pTags[i].TagNumber = 0;
		strcpy(INI_data.pTags[i].ProjectName, "");
		strcpy(INI_data.pTags[i].NodeName, "");
		INI_data.pTags[i].PortNumber = 0;
		strcpy(INI_data.pTags[i].DeviceName, "");
		strcpy(INI_data.pTags[i].TagName, "");
		INI_data.pTags[i].TagType = 0;
		INI_data.pTags[i].TagValue = 0;
		INI_data.pTags[i].TagAvailable = false;
	}
}


bool CheckProjectSync(MSG_CLASSIFY_T *parentGroup)
{
	int row;

	MSG_CLASSIFY_T *targetGroup = parentGroup;
	if(targetGroup == NULL)
	{
		return false;
	}

	for (row = 0; row < MAX_NAMELIST_NUM; row++)
	{
		if(projectNameList[row][0] != 0)
		{
			if(targetGroup->sub_list == NULL || targetGroup->sub_list->sub_list == NULL || targetGroup->sub_list->sub_list->classname == NULL)
			{
				return false;
			}
			
			int ret = strcmp(targetGroup->sub_list->sub_list->classname, projectNameList[row]);

			if(ret != 0 || targetGroup==NULL)
			{
				return false;
			}
			else
			{
				targetGroup = targetGroup->next;
			}
		}
		else
		{
			if(targetGroup != NULL && targetGroup->sub_list != NULL && targetGroup->sub_list->sub_list != NULL && targetGroup->sub_list->sub_list->classname != NULL)
			{
				return false;
			}
		}
	}

	return true;
}


bool CheckNodeSync(MSG_CLASSIFY_T *parentGroup)
{
	int row;
	MSG_CLASSIFY_T *targetGroup = parentGroup;

	for (row = 0; row < MAX_NAMELIST_NUM; row++)
	{
		if(nodeNameList[row][0] != 0)
		{
			if(targetGroup->sub_list == NULL || targetGroup->sub_list->sub_list == NULL || targetGroup->sub_list->sub_list->classname == NULL)
			{
				return false;
			}
			
			int ret = strcmp(targetGroup->sub_list->sub_list->classname, nodeNameList[row]);

			if(ret != 0 || targetGroup==NULL)
			{
				return false;
			}
			else
			{
				targetGroup = targetGroup->next;
			}
		}
	}
	return true;
}


bool CheckPortSync(MSG_CLASSIFY_T *parentGroup)
{
	int row;
	MSG_CLASSIFY_T *targetGroup = parentGroup;

	for (row = 0; row < MAX_NAMELIST_NUM; row++)
	{
		if(portNameList[row][0] != 0)
		{
			if(targetGroup->sub_list == NULL || targetGroup->sub_list->sub_list == NULL || targetGroup->sub_list->sub_list->classname == NULL)
			{
				return false;
			}
			
			char strPortNumber[30];
			itoa(portNumberList[row], strPortNumber, 10);
			int ret = strcmp(targetGroup->sub_list->sub_list->classname, strPortNumber);

			if(ret != 0 || targetGroup==NULL)
			{
				return false;
			}
			else
			{
				targetGroup = targetGroup->next;
			}
		}
	}
	return true;
}


bool CheckDeviceSync(MSG_CLASSIFY_T *parentGroup)
{
	int row;
	MSG_CLASSIFY_T *targetGroup = parentGroup;

	for (row = 0; row < MAX_NAMELIST_NUM; row++)
	{
		if(deviceNameList[row][0] != 0)
		{
			if(targetGroup->sub_list == NULL || targetGroup->sub_list->sub_list == NULL || targetGroup->sub_list->sub_list->classname == NULL)
			{
				return false;
			}
			
			int ret = strcmp(targetGroup->sub_list->sub_list->classname, deviceNameList[row]);

			if(ret != 0 || targetGroup==NULL)
			{
				return false;
			}
			else
			{
				targetGroup = targetGroup->next;
			}
		}
	}
	return true;
}


bool CheckTagSync(MSG_CLASSIFY_T *parentGroup)
{
	int row;
	MSG_CLASSIFY_T *targetGroup = parentGroup;

	for (row = 0; row < MAX_NAMELIST_NUM; row++)
	{
		if(tagNameList[row][0] != 0)
		{
			if(targetGroup->sub_list == NULL || targetGroup->sub_list->sub_list == NULL || targetGroup->sub_list->sub_list->classname == NULL)
			{
				return false;
			}
			
			int ret = strcmp(targetGroup->sub_list->sub_list->classname, tagNameList[row]);

			if(ret != 0 || targetGroup==NULL)
			{
				return false;
			}
			else
			{
				targetGroup = targetGroup->next;
			}
		}
	}
	return true;
}


bool UpdateProjectList(MSG_CLASSIFY_T *parentGroup)
{
	char jsonNodeNameList[MAX_JSON_BUF_LEN];
	int row;
	int size = 0;

	INI_data.numberOfTags = 0;

	if(parentGroup == NULL)
	{
		return false;
	}

	for (row = 0; row < MAX_NAMELIST_NUM; row++)
	{
		if(projectNameList[row][0] != 0)
		{	
			MSG_CLASSIFY_T *projectGroup = IoT_FindGroup(parentGroup, projectNameList[row]);
			if(projectGroup == NULL)
			{
				projectGroup = IoT_AddGroup(parentGroup, projectNameList[row]);

				//const char* jsonNodeNameList = GetNodeList(projectNameList[row]);
				if (WebAccess_GetNodeList(jsonNodeNameList, &size, projectNameList[row]))
				{
					if(ParseNodeList(jsonNodeNameList) == true)
					{
						UpdateNodeList(projectGroup, projectNameList[row]);
					}
				}
			}
			else
			{
				//projectGroup = IoT_FindGroup(g_Capability, projectNameList[row]);

				//const char* jsonNodeNameList = GetNodeList(projectNameList[row]);
				if (WebAccess_GetNodeList(jsonNodeNameList, &size, projectNameList[row]))
				{
					if(ParseNodeList(jsonNodeNameList) == true)
					{
						UpdateNodeList(projectGroup, projectNameList[row]);
					}
				}
			}
		}
		else
		{
			break;
		}
	}
	return true;
}


bool UpdateNodeList(MSG_CLASSIFY_T *parentGroup, char *projectName)
{
	char jsonPortNameList[MAX_JSON_BUF_LEN];
	int row;
	int size = 0;

	if(parentGroup == NULL || projectName == NULL)
	{
		return false;
	}

	for (row = 0; row < MAX_NAMELIST_NUM; row++)
	{
		if(nodeNameList[row][0] != 0)
		{	
			MSG_CLASSIFY_T *nodeGroup = IoT_FindGroup(parentGroup, nodeNameList[row]);
			if(nodeGroup == NULL)
			{
				nodeGroup = IoT_AddGroup(parentGroup, nodeNameList[row]);

				//const char* jsonPortNameList = GetPortList(nodeNameList[row]);
				if (WebAccess_GetPortList(jsonPortNameList, &size, projectName, nodeNameList[row]))
				{
					if(ParsePortList(jsonPortNameList) == true)
					{
						UpdatePortList(nodeGroup, projectName, nodeNameList[row]);
					}
				}
			}
			else
			{
				//nodeGroup = IoT_FindGroup(parentGroup, nodeNameList[row]);

				//const char* jsonPortNameList = GetPortList(nodeNameList[row]);
				if (WebAccess_GetPortList(jsonPortNameList, &size, projectName, nodeNameList[row]))
				{
					if(ParsePortList(jsonPortNameList) == true)
					{
						UpdatePortList(nodeGroup, projectName, nodeNameList[row]);
					}
				}
			}
		}
		else
		{
			break;
		}
	}

	MSG_CLASSIFY_T *targetGroup = parentGroup->sub_list;
	while(targetGroup != NULL)
	{
		for (row = 0; row < MAX_NAMELIST_NUM; row++)
		{
			if(nodeNameList[row][0] != '\0')
			{
				if(strcmp(targetGroup->classname, nodeNameList[row]) == 0)
				{
					// node found in nodeNameList
					targetGroup = targetGroup->next;
					break;
				}
			}
			else
			{
				// node not found in nodeNameList
				MSG_CLASSIFY_T *tempGroup = targetGroup;
				targetGroup = targetGroup->next;

				if(tempGroup != NULL)
				{
					IoT_DelGroup(parentGroup, tempGroup->classname);
				}
				break;
			}
		}
	}

	return true;
}


bool UpdatePortList(MSG_CLASSIFY_T *parentGroup, char *projectName, char *nodeName)
{
	char jsonDeviceNameList[MAX_JSON_BUF_LEN];
	int row;
	int size = 0;

	if(parentGroup == NULL /*|| projectName == NULL || nodeName == NULL*/)
	{
		return false;
	}

	for (row = 0; row < MAX_NAMELIST_NUM; row++)
	{
		if(portNameList[row][0] != 0)
		{	
			char strPortNumber[30];
			itoa(portNumberList[row], strPortNumber, 10);

			MSG_CLASSIFY_T *portGroup = IoT_FindGroup(parentGroup, strPortNumber);
			//MSG_CLASSIFY_T *portGroup = IoT_FindGroup(parentGroup, portNameList[row]);
			
			if(portGroup == NULL)
			{
				//portGroup = IoT_AddGroup(parentGroup, portNameList[row]);
				portGroup = IoT_AddGroup(parentGroup, strPortNumber);

				//const char* jsonDeviceNameList = GetDeviceList(portNameList[row]);
				if (WebAccess_GetDeviceList(jsonDeviceNameList, &size, projectName, nodeName, portNumberList[row]))
				{
					if(ParseDeviceList(jsonDeviceNameList) == true)
					{
						UpdateDeviceList(portGroup, projectName, nodeName, portNumberList[row]);
					}
				}
			}
			else
			{
				char strPortNumber[30];
				itoa(portNumberList[row], strPortNumber, 10);

				//portGroup = IoT_FindGroup(parentGroup, portNameList[row]);
				//portGroup = IoT_FindGroup(parentGroup, strPortNumber);

				//const char* jsonDeviceNameList = GetDeviceList(portNameList[row]);
				//if (WebAccess_GetDeviceList(jsonDeviceNameList, &size, projectName, nodeName, portNameList[row]))
				if (WebAccess_GetDeviceList(jsonDeviceNameList, &size, projectName, nodeName, portNumberList[row]))
				{
					if(ParseDeviceList(jsonDeviceNameList) == true)
					{
						UpdateDeviceList(portGroup, projectName, nodeName, portNumberList[row]);
					}
				}
			}
		}
		else
		{
			break;
		}
	}

	MSG_CLASSIFY_T *targetGroup = parentGroup->sub_list;
	while(targetGroup != NULL)
	{
		for (row = 0; row < MAX_NAMELIST_NUM; row++)
		{
			if(portNameList[row][0] != '\0')
			{
				char strPortNumber[30];
				itoa(portNumberList[row], strPortNumber, 10);

				if(strcmp(targetGroup->classname, strPortNumber) == 0)
				{
					// node found in portNameList
					targetGroup = targetGroup->next;
					break;
				}
			}
			else
			{
				// node not found in portNameList
				MSG_CLASSIFY_T *tempGroup = targetGroup;
				targetGroup = targetGroup->next;

				if(tempGroup != NULL)
				{
					IoT_DelGroup(parentGroup, tempGroup->classname);
				}
				break;
			}
		}
	}

	return true;
}


bool UpdateDeviceList(MSG_CLASSIFY_T *parentGroup, char *projectName, char *nodeName, int portNumber)
{
	char jsonTagNameList[MAX_JSON_BUF_LEN];
	int row;
	int size = 0;


	if(parentGroup == NULL /*|| projectName == NULL || nodeName == NULL || portNumber == NULL*/)
	{
		return false;
	}

	for (row = 0; row < MAX_NAMELIST_NUM; row++)
	{
		if(deviceNameList[row][0] != 0)
		{	
			MSG_CLASSIFY_T *deviceGroup = IoT_FindGroup(parentGroup, deviceNameList[row]);
			
			if(deviceGroup == NULL)
			{
				deviceGroup = IoT_AddGroup(parentGroup, deviceNameList[row]);

				//const char* jsonTagNameList = GetTagList(deviceNameList[row]);
				//if (WebAccess_GetTagList(jsonTagNameList, &size, projectName, nodeName, portName, deviceNameList[row]))
				if (WebAccess_GetTagList(jsonTagNameList, &size, projectName, nodeName, portNumber, deviceNameList[row]))
				{
					ParseTagList(jsonTagNameList);
					UpdateTagList(deviceGroup, projectName, nodeName, portNumber, deviceNameList[row]);
				}
			}
			else
			{
				//deviceGroup = IoT_FindGroup(parentGroup, deviceNameList[row]);

				//const char* jsonTagNameList = GetTagList(deviceNameList[row]);
				//if (WebAccess_GetTagList(jsonTagNameList, &size, projectName, nodeName, portName, deviceNameList[row]))
				if (WebAccess_GetTagList(jsonTagNameList, &size, projectName, nodeName, portNumber, deviceNameList[row]))
				{
					ParseTagList(jsonTagNameList);
					UpdateTagList(deviceGroup, projectName, nodeName, portNumber, deviceNameList[row]);
				}
			}
		}
		else
		{
			break;
		}
	}

	MSG_CLASSIFY_T *targetGroup = parentGroup->sub_list;
	while(targetGroup != NULL)
	{
		for (row = 0; row < MAX_NAMELIST_NUM; row++)
		{
			if(deviceNameList[row][0] != '\0')
			{
				if(strcmp(targetGroup->classname, deviceNameList[row]) == 0)
				{
					// node found in deviceNameList
					targetGroup = targetGroup->next;
					break;
				}
			}
			else
			{
				// node not found in deviceNameList
				MSG_CLASSIFY_T *tempGroup = targetGroup;
				targetGroup = targetGroup->next;

				if(tempGroup != NULL)
				{
					IoT_DelGroup(parentGroup, tempGroup->classname);
				}
				break;
			}
		}
	}
	return true;
}



bool UpdateTagList(MSG_CLASSIFY_T *parentGroup, char *projectName, char *nodeName, int portNumber, char *deviceName)
{
	char jsonTagValueLocal[MAX_JSON_BUF_LEN];
	char jsonTagDetailLocal[MAX_JSON_BUF_LEN];
	int row;
	int size = 0;

	if(parentGroup == NULL /*|| projectName == NULL || nodeName == NULL || portNumber == NULL || deviceName == NULL*/)
	{
		return false;
	}

	for (row = 0; row < MAX_NAMELIST_NUM; row++)
	{
		if(tagNameList[row][0] != 0)
		{
			strcpy(INI_data.pTags[INI_data.numberOfTags].TagName, tagNameList[row]);
			strcpy(INI_data.pTags[INI_data.numberOfTags].ProjectName, projectName);
			strcpy(INI_data.pTags[INI_data.numberOfTags].NodeName, nodeName);
			INI_data.pTags[INI_data.numberOfTags].PortNumber = portNumber;
			strcpy(INI_data.pTags[INI_data.numberOfTags].DeviceName, deviceName);
			//printf("Add Tag: %s into Tags[%d]\r\n", INI_data.pTags[INI_data.numberOfTags].TagName, INI_data.numberOfTags);
			INI_data.numberOfTags++;
		}
		else
		{
			break;
		}
	}

	MSG_CLASSIFY_T *targetGroup = parentGroup->sub_list;
	while(targetGroup != NULL)
	{
		for (row = 0; row < MAX_NAMELIST_NUM; row++)
		{
			if(tagNameList[row][0] != '\0')
			{
				if(strcmp(targetGroup->classname, tagNameList[row]) == 0)
				{
					// node found in tagNameList
					targetGroup = targetGroup->next;
					break;
				}
			}
			else
			{
				// node not found in tagNameList
				MSG_CLASSIFY_T *tempGroup = targetGroup;
				targetGroup = targetGroup->next;
				IoT_DelGroup(parentGroup, tempGroup->classname);
				break;
			}
		}
	}
	return true;
}


bool UpdateTagValueByIndex(MSG_CLASSIFY_T *parentGroup, int index)
{
	char name[100] = "";

	if(parentGroup != NULL && INI_data.pTags[index].TagName != NULL)
	{
		if(INI_data.pTags[index].TagAvailable == true)
		{
			//printf("UpdatingTagValue: index=%d, TagName=%s, value=\r\n", index, INI_data.pTags[index].TagName);

			MSG_ATTRIBUTE_T* sensor = IoT_FindSensorNode(parentGroup, INI_data.pTags[index].TagName);
			if(sensor == NULL)
			{
				sensor = IoT_AddSensorNode(parentGroup, INI_data.pTags[index].TagName);
			}
			IoT_SetStringValue(sensor, INI_data.pTags[index].TagName, IoT_READONLY);


			if (INI_data.pTags[index].TagType == TAG_TYPE_ANALOG)
			{
				IoT_SetDoubleValue(sensor, INI_data.pTags[index].TagValue, (IoT_READWRITE_MODE)INI_data.pTags[index].Readonly, NULL);
			}
			else if (INI_data.pTags[index].TagType == TAG_TYPE_DIGITAL)
			{
				bool bValue = true;
				if (INI_data.pTags[index].TagValue == 0)
				{
					bValue = false;
				}
				IoT_SetBoolValue(sensor, bValue, (IoT_READWRITE_MODE)INI_data.pTags[index].Readonly);
			}
			else if (INI_data.pTags[index].TagType == TAG_TYPE_TEXT)
			{
				IoT_SetStringValue(sensor, INI_data.pTags[index].TagValueText, (IoT_READWRITE_MODE)INI_data.pTags[index].Readonly);
			}
			else
			{
				IoT_SetDoubleValue(sensor, INI_data.pTags[index].TagValue, (IoT_READWRITE_MODE)INI_data.pTags[index].Readonly, NULL);
			}
			MSG_AppendIoTSensorAttributeDouble(sensor, "q", INI_data.pTags[index].TagQuality);


			// Debug only
			if(INI_data.pTags[index].TagType == TAG_TYPE_TEXT)
			{
				WASCADAHLog(Debug, "Updating Tag Value: Index=%d, Project=%s, Node=%s, Port=%d, Device=%s, Tag=%s, Value=%s",
					index, 
					INI_data.pTags[index].ProjectName,
					INI_data.pTags[index].NodeName,
					INI_data.pTags[index].PortNumber,
					INI_data.pTags[index].DeviceName,
					INI_data.pTags[index].TagName,
					INI_data.pTags[index].TagValueText);
			}
			else
			{
				WASCADAHLog(Debug, "Updating Tag Value: Index=%d, Project=%s, Node=%s, Port=%d, Device=%s, Tag=%s, Value=%f",
					index, 
					INI_data.pTags[index].ProjectName,
					INI_data.pTags[index].NodeName,
					INI_data.pTags[index].PortNumber,
					INI_data.pTags[index].DeviceName,
					INI_data.pTags[index].TagName,
					INI_data.pTags[index].TagValue);
			}
			// End of debug

		}
		else
		{
			return false;
		}
	}
	return true;
}



void ReportTagUnavailableByIndex(MSG_CLASSIFY_T *parentGroup, int index)
{
	if(parentGroup != NULL && INI_data.pTags[index].TagName != NULL)
	{
		if(INI_data.pTags[index].TagAvailable == false)
		{	
			// Tag's detail information is unavailable.
			MSG_ATTRIBUTE_T* sensor = IoT_FindSensorNode(parentGroup, INI_data.pTags[index].TagName);
			if(sensor == NULL)
			{
				sensor = IoT_AddSensorNode(parentGroup, INI_data.pTags[index].TagName);
			}
			//IoT_SetStringValue(sensor, INI_data.pTags[index].TagName, IoT_READONLY);

			IoT_SetStringValue(sensor, "Unavailable", (IoT_READWRITE_MODE)INI_data.pTags[index].Readonly);
			strcpy(INI_data.pTags[index].TagValueText, "Unavailable");
			MSG_AppendIoTSensorAttributeDouble(sensor, "q", INI_data.pTags[index].TagQuality);

			WASCADAHLog(Debug, "Reportting Unavailable Tag: Index=%d, Project=%s, Tag=%s, Value=%s",
				index, 
				INI_data.pTags[index].ProjectName,
				INI_data.pTags[index].TagName,
				INI_data.pTags[index].TagValueText);
		}
	}
}


int UpdateTagValueByName(MSG_CLASSIFY_T *parentGroup, char* targetName)
{
	int tagIndex = 0;

	char jsonTagValueLocal[MAX_JSON_BUF_LEN];
	int size = 0;

	if(targetName == NULL)
	{
		tagIndex = 0;
	}

	if(parentGroup == NULL)
	{
		return false;
	}

	for(int i=0; i<INI_data.numberOfTags; i++)
	{
		if(strcmp(INI_data.pTags[i].TagName, targetName) == 0)
		{
			if(INI_data.pTags[i].TagAvailable == true)
			{
				// Project
				MSG_CLASSIFY_T *projectGroup = IoT_FindGroup(parentGroup, INI_data.pTags[i].ProjectName);
				if (projectGroup == NULL)
				{
					projectGroup = IoT_AddGroup(parentGroup, INI_data.pTags[i].ProjectName);
				}

				// Node
				MSG_CLASSIFY_T *nodeGroup = IoT_FindGroup(projectGroup, INI_data.pTags[i].NodeName);
				if (nodeGroup == NULL)
				{
					nodeGroup = IoT_AddGroup(projectGroup, INI_data.pTags[i].NodeName);
				}

				// Port
				char strPortNumber[30];
				itoa(INI_data.pTags[i].PortNumber, strPortNumber, 10);

				MSG_CLASSIFY_T *portGroup = IoT_FindGroup(nodeGroup, strPortNumber);
				if (portGroup == NULL)
				{
					portGroup = IoT_AddGroup(nodeGroup, strPortNumber);
				}

				// Device
				MSG_CLASSIFY_T *tempGroup = NULL;
				if (strlen(INI_data.pTags[i].DeviceName) != 0)
				{
					MSG_CLASSIFY_T *deviceGroup = IoT_FindGroup(portGroup, INI_data.pTags[i].DeviceName);
					if (deviceGroup == NULL)
					{
						deviceGroup = IoT_AddGroup(portGroup, INI_data.pTags[i].DeviceName);
					}
					tempGroup = deviceGroup;
				}
				else
				{
					tempGroup = nodeGroup;
				}

				// Value
				if (INI_data.pTags[i].TagType == TAG_TYPE_TEXT)
				{
					if (WebAccess_GetTagValueText(jsonTagValueLocal, &size, INI_data.pTags[i].ProjectName, INI_data.pTags[i].TagName))
					{
						ParseTagValueText(jsonTagValueLocal, i);
						UpdateTagValueByIndex(tempGroup, i);
					}
				}
				else
				{
					if (WebAccess_GetTagValue(jsonTagValueLocal, &size, INI_data.pTags[i].ProjectName, INI_data.pTags[i].TagName))
					{
						ParseTagValue(jsonTagValueLocal, i);
						UpdateTagValueByIndex(tempGroup, i);
					}
				}
			}
			else
			{
				// Project
				MSG_CLASSIFY_T *projectGroup = IoT_FindGroup(parentGroup, INI_data.pTags[i].ProjectName);
				if (projectGroup == NULL)
				{
					projectGroup = IoT_AddGroup(parentGroup, INI_data.pTags[i].ProjectName);
				}

				ReportTagUnavailableByIndex(projectGroup, i);
			}

			tagIndex = i;
			break;
		}
	}
	return tagIndex;
}


bool ParseProjectList(const char* jsonList)
{
	cJSON* root = NULL;
	cJSON* target = NULL;
	MSG_ATTRIBUTE_T* attr = NULL;
	int total = 0;

	if(!jsonList || !strcmp(jsonList, "null"))
	{
		return false;
	}

	InitProjectNameList();

	root = cJSON_Parse(jsonList);
	if(!root)
	{
		return false;
	}

	// Result
    cJSON *jsonResult = cJSON_GetObjectItem(root, "Result");
	if(jsonResult == NULL)
	{
		if(root != NULL)
		{
			cJSON_Delete(root);
		}
		return false;
	}

    //int ret = cJSON_GetObjectItem(jsonResult, "Ret")->valueint;
    //int total = cJSON_GetObjectItem(jsonResult, "Total")->valueint;

	cJSON *jsonTotal = cJSON_GetObjectItem(jsonResult, "Total");
	if(jsonTotal != NULL)
	{
		total = jsonTotal->valueint;
	}

    // Projects
    cJSON *jsonProject = cJSON_GetObjectItem(root, "Projects");
	if(jsonProject != NULL)
	{
		cJSON *jsonNext = jsonProject->child;

		for(int i=0; i<total; i++)
		{
			if(jsonNext != NULL)
			{
				/*
				cJSON *jsonID = cJSON_GetObjectItem(jsonNext, "ID");
				if(jsonID != NULL)
				{
					int id = jsonID->valueint;
				}
				*/

				char *name = cJSON_Print(cJSON_GetObjectItem(jsonNext, "Name"));
				if(name != NULL)
				{
					char *temp = name + 1;
					if(temp != NULL &&  strlen(temp) > 0)
					{
						strncpy(projectNameList[i], temp, strlen(temp)-1);
					}
					free(name);
				}
				jsonNext = jsonNext->next;
			}
		}
	}

	if(root != NULL)
	{
		cJSON_Delete(root);
	}
	return true;
}


bool ParseNodeList(const char* jsonList)
{
	cJSON* root = NULL;
	cJSON* target = NULL;
	MSG_ATTRIBUTE_T* attr = NULL;
	int total = 0;

	if(!jsonList  || !strcmp(jsonList, "null"))
	{
		return false;
	}

	InitNodeNameList();

	root = cJSON_Parse(jsonList);
	if(!root)
	{
		return false;
	}

	// Result
    cJSON *jsonResult = cJSON_GetObjectItem(root, "Result");
	if(jsonResult == NULL)
	{
		if(root != NULL)
		{
			cJSON_Delete(root);
		}
		return false;
	}

    //int ret = cJSON_GetObjectItem(jsonResult, "Ret")->valueint;
    //int total = cJSON_GetObjectItem(jsonResult, "Total")->valueint;

	cJSON *jsonTotal = cJSON_GetObjectItem(jsonResult, "Total");
	if(jsonTotal != NULL)
	{
		total = jsonTotal->valueint;
	}

    // Nodes
    cJSON *jsonNodes = cJSON_GetObjectItem(root, "Nodes");
	if(jsonNodes != NULL)
	{
		cJSON *jsonNext = jsonNodes->child;

		for(int i=0; i<total; i++)
		{
			if(jsonNext != NULL)
			{
				// NodeName
				char *name = cJSON_Print(cJSON_GetObjectItem(jsonNext, "NodeName"));
				if(name != NULL)
				{				
					char *temp = name + 1;
					if(temp != NULL && strlen(temp) >0)
					{
						strncpy(nodeNameList[i], temp, strlen(temp)-1);
					}
					free(name);
				}
				jsonNext = jsonNext->next;
			}
		}
	}

	if(root != NULL)
	{
		cJSON_Delete(root);
	}
	return true;
}


bool ParsePortList(const char* jsonList)
{
	cJSON* root = NULL;
	cJSON* target = NULL;
	MSG_ATTRIBUTE_T* attr = NULL;
	int total = 0;

	if(!jsonList || !strcmp(jsonList, "null"))
	{
		return false;
	}

	InitPortNameList();

	root = cJSON_Parse(jsonList);
	if(!root)
	{
		return false;
	}

	// Result
    cJSON *jsonResult = cJSON_GetObjectItem(root, "Result");
	if(jsonResult == NULL)
	{
		if(root != NULL)
		{
			cJSON_Delete(root);
		}
		return false;
	}

    //int ret = cJSON_GetObjectItem(jsonResult, "Ret")->valueint;
    //int total = cJSON_GetObjectItem(jsonResult, "Total")->valueint;

	cJSON *jsonTotal = cJSON_GetObjectItem(jsonResult, "Total");
	if(jsonTotal != NULL)
	{
		total = jsonTotal->valueint;
	}

    // Ports
    cJSON *jsonPorts = cJSON_GetObjectItem(root, "Ports");
	if(jsonPorts != NULL)
	{
		cJSON *jsonNext = jsonPorts->child;

		for(int i=0; i<total; i++)
		{
			if(jsonNext != NULL)
			{
				// InterfaceName
				char *name = cJSON_Print(cJSON_GetObjectItem(jsonNext, "InterfaceName"));
				if(name != NULL)
				{
					char *temp = name + 1;
					if(temp != NULL && strlen(temp) >0)
					{
						strncpy(portNameList[i], temp, strlen(temp)-1);
					}
					free(name);
				}

				// PortNumber
				cJSON *jsonPortNumber = cJSON_GetObjectItem(jsonNext, "PortNumber");
				if(jsonPortNumber != NULL)
				{
					portNumberList[i] = jsonPortNumber->valueint;
				}
				jsonNext = jsonNext->next;
			}
		}
	}

	if(root != NULL)
	{
		cJSON_Delete(root);
	}
	return true;
}


bool ParseDeviceList(const char* jsonList)
{
	cJSON* root = NULL;
	cJSON* target = NULL;
	MSG_ATTRIBUTE_T* attr = NULL;
	int total = 0;

	if(!jsonList || !strcmp(jsonList, "null"))
	{
		return false;
	}

	InitDeviceNameList();

	root = cJSON_Parse(jsonList);
	if(!root)
	{
		return false;
	}

	// Result
    cJSON *jsonResult = cJSON_GetObjectItem(root, "Result");
	if(jsonResult == NULL)
	{
		if(root != NULL)
		{
			cJSON_Delete(root);
		}
		return false;
	}

    //int ret = cJSON_GetObjectItem(jsonResult, "Ret")->valueint;
    //int total = cJSON_GetObjectItem(jsonResult, "Total")->valueint;

	cJSON *jsonTotal = cJSON_GetObjectItem(jsonResult, "Total");
	if(jsonTotal != NULL)
	{
		total = jsonTotal->valueint;
	}

    // Devices
    cJSON *jsonDevices = cJSON_GetObjectItem(root, "Devices");
	if(jsonDevices != NULL)
	{
		cJSON *jsonNext = jsonDevices->child;

		for(int i=0; i<total; i++)
		{
			if(jsonNext != NULL)
			{
				// DeviceName
				char *name = cJSON_Print(cJSON_GetObjectItem(jsonNext, "DeviceName"));
				if(name != NULL)
				{
					char *temp = name + 1;
					if(temp != NULL && strlen(temp) > 0)
					{
						strncpy(deviceNameList[i], temp, strlen(temp)-1);
					}
					free(name);
				}
				jsonNext = jsonNext->next;
			}
		}
	}

	if(root != NULL)
	{
		cJSON_Delete(root);
	}
	return true;
}


bool ParseTagList(const char* jsonList)
{
	cJSON* root = NULL;
	cJSON* target = NULL;
	MSG_ATTRIBUTE_T* attr = NULL;
	int total = 0;

	if(!jsonList || !strcmp(jsonList, "null"))
	{
		return false;
	}

	InitTagNameList();

	root = cJSON_Parse(jsonList);
	if(!root)
	{
		WASCADAHLog(Error, "\r\ParseTagList: Parse failed, JSON string format error: %s\r\n\n");
		return false;
	}

	// Result
    cJSON *jsonResult = cJSON_GetObjectItem(root, "Result");
	if(jsonResult == NULL)
	{
		WASCADAHLog(Error, "\r\ParseTagList: Parse failed, no Result in JSON string\r\n\n");

		if(root != NULL)
		{
			cJSON_Delete(root);
		}
		return false;
	}

    //int ret = cJSON_GetObjectItem(jsonResult, "Ret")->valueint;
    //int total = cJSON_GetObjectItem(jsonResult, "Total")->valueint;

	cJSON *jsonTotal = cJSON_GetObjectItem(jsonResult, "Total");
	if(jsonTotal != NULL)
	{
		total = jsonTotal->valueint;
	}

	//INI_data.numberOfTags = INI_data.numberOfTags + total;

    // Tags
    cJSON *jsonTags = cJSON_GetObjectItem(root, "Tags");
	if(jsonTags != NULL)
	{
		cJSON *jsonNext = jsonTags->child;

		for(int i=0; i<total; i++)
		{
			if(jsonNext != NULL)
			{
				char *name = cJSON_Print(cJSON_GetObjectItem(jsonNext, "Name"));
				if(name != NULL)
				{
					char *temp = name + 1;
					if(temp != NULL && strlen(temp) > 0)
					{
						strncpy(tagNameList[i], temp, strlen(temp)-1);
					}
					free(name);
				}

				jsonNext = jsonNext->next;
			}
		}
	}

	if(root != NULL)
	{
		cJSON_Delete(root);
	}
	return false;
}


bool ParseTagValue(const char* jsonTagValue, int index)
{
	cJSON* root = NULL;
	cJSON* target = NULL;
	MSG_ATTRIBUTE_T* attr = NULL;

	if(!jsonTagValue || !strcmp(jsonTagValue, "null"))
	{
		return false;
	}

	//InitTagValue();
	root = cJSON_Parse(jsonTagValue);
	if(!root)
	{
		WASCADAHLog(Error, "\r\nParseTagValue: Parse failed, JSON string format error: %s\r\n\n", jsonTagValue);
		return false;
	}

	// Result
    cJSON *jsonResult = cJSON_GetObjectItem(root, "Result");
	if(jsonResult == NULL)
	{
		WASCADAHLog(Error, "\r\nParseTagValue: Parse failed, no Result in JSON string: %s\r\n\n", jsonTagValue);
		
		if(root != NULL)
		{
			cJSON_Delete(root);
		}
		return false;
	}

	//int ret = cJSON_GetObjectItem(jsonResult, "Ret")->valueint;
	//int total = cJSON_GetObjectItem(jsonResult, "Total")->valueint;

    // Tags
    cJSON *jsonValues = cJSON_GetObjectItem(root, "Values");
	if(jsonValues != NULL)
	{
		cJSON *jsonNext = jsonValues->child;
		if(jsonNext != NULL)
		{
			cJSON *jsonVal = cJSON_GetObjectItem(jsonNext, "Value");
			if(jsonVal != NULL)
			{
				// Value
				INI_data.pTags[index].TagValue = jsonVal->valuedouble;
			}
			else
			{
				WASCADAHLog(Error, "\r\nParseTagValue: Parse failed, no Value in JSON string: %s\r\n\n", jsonTagValue);
			}

			cJSON *jsonQuality = cJSON_GetObjectItem(jsonNext, "Quality");
			if(jsonQuality != NULL)
			{
				// Quality
				INI_data.pTags[index].TagQuality = jsonQuality->valueint;
			}
			else
			{
				WASCADAHLog(Error, "\r\nParseTagValue: Parse failed, no Quality in JSON string: %s\r\n\n", jsonTagValue);
			}

			/*
			// Value
			INI_data.pTags[index].TagValue = cJSON_GetObjectItem(jsonNext, "Value")->valuedouble;
			// Quality
			INI_data.pTags[index].TagQuality = cJSON_GetObjectItem(jsonNext, "Quality")->valueint;
			*/
		}
	}
	else
	{
		WASCADAHLog(Error, "\r\nParseTagValue: Parse failed, no Values in JSON string: %s\r\n\n", jsonTagValue);
	}

	if(root != NULL)
	{
		cJSON_Delete(root);
	}
	return true;
}

bool ParseTagValueText(const char* jsonTagValueText, int index)
{
	cJSON* root = NULL;
	cJSON* target = NULL;
	MSG_ATTRIBUTE_T* attr = NULL;

	if(!jsonTagValueText || !strcmp(jsonTagValueText, "null"))
	{
		return false;
	}

	//InitTagValue();
	root = cJSON_Parse(jsonTagValueText);
	if(!root)
	{
		WASCADAHLog(Error, "\r\ParseTagValueText: Parse failed, JSON string format error: %s\r\n\n", jsonTagValueText);
		return false;
	}

	// Result
    cJSON *jsonResult = cJSON_GetObjectItem(root, "Result");
	if(jsonResult == NULL)
	{
		WASCADAHLog(Error, "\r\ParseTagValueText: Parse failed, no Result in JSON string: %s\r\n\n", jsonTagValueText);

		if(root != NULL)
		{
			cJSON_Delete(root);
		}
		return false;
	}

	//int ret = cJSON_GetObjectItem(jsonResult, "Ret")->valueint;
	//int total = cJSON_GetObjectItem(jsonResult, "Total")->valueint;

    // Tags
    cJSON *jsonValues = cJSON_GetObjectItem(root, "Values");
	if(jsonValues != NULL)
	{
		cJSON *jsonNext = jsonValues->child;
		if(jsonNext != NULL)
		{
			// Value
			char *value = cJSON_Print(cJSON_GetObjectItem(jsonNext, "Value"));
			if(value != NULL)
			{
				char *temp2 = value + 1;
				strncpy(INI_data.pTags[index].TagValueText, temp2, strlen(temp2)-1);

				if(value != NULL)
				{
					free(value);
				}
			}
			else
			{
				WASCADAHLog(Error, "\r\ParseTagValueText: Parse failed, no Value in JSON string: %s\r\n\n", jsonTagValueText);
			}

			// Quality
			cJSON *jsonQuality = cJSON_GetObjectItem(jsonNext, "Quality");
			if(jsonQuality != NULL)
			{
				INI_data.pTags[index].TagQuality = jsonQuality->valueint;
			}
			else
			{
				WASCADAHLog(Error, "\r\ParseTagValueText: Parse failed, no Quality in JSON string: %s\r\n\n", jsonTagValueText);
			}

			//INI_data.pTags[index].TagQuality = cJSON_GetObjectItem(jsonNext, "Quality")->valueint;
		}
	}
	else
	{
		WASCADAHLog(Error, "\r\ParseTagValueText: Parse failed, no Values in JSON string: %s\r\n\n", jsonTagValueText);
	}

	if(root != NULL)
	{
		cJSON_Delete(root);
	}
	return false;
}

bool ParseTagDetail(const char* jsonTagDetail, int index, bool getTypeOnly)
{
	cJSON* root = NULL;
	cJSON* target = NULL;
	MSG_ATTRIBUTE_T* attr = NULL;

	if(!jsonTagDetail || !strcmp(jsonTagDetail, "null"))
	{
		return false;
	}

	root = cJSON_Parse(jsonTagDetail);
	if(!root)
	{
		WASCADAHLog(Error, "\r\nParseTagValue: Parse failed, JSON string format error: %s\r\n\n", jsonTagDetail);
		return false;
	}

	// Result
    cJSON *jsonResult = cJSON_GetObjectItem(root, "Result");
	if(jsonResult == NULL)
	{
		WASCADAHLog(Error, "\r\ParseTagValueText: Parse failed, no Result in JSON string: %s\r\n\n", jsonTagDetail);

		if(root != NULL)
		{
			cJSON_Delete(root);
		}
		return false;
	}

	//int ret = cJSON_GetObjectItem(jsonResult, "Ret")->valueint;

	// Total
	cJSON *jsonTotal = cJSON_GetObjectItem(jsonResult, "Total");
	if(jsonTotal != NULL)
	{
		int total = jsonTotal->valueint;
		if (!total)
		{
			return false;
		}
	}

	// Tags
	cJSON *jsonTags = cJSON_GetObjectItem(root, "Tags");
	if(jsonTags != NULL)
	{
		cJSON *jsonNext = jsonTags->child;
		if(jsonNext != NULL)
		{
			if (!getTypeOnly)
			{
				// Node
				cJSON* jsonNode = cJSON_GetObjectItem(jsonNext, "NODE");
				if(jsonNode != NULL)
				{
					strcpy(INI_data.pTags[index].NodeName, jsonNode->valuestring);
				}
				else
				{
					strcpy(INI_data.pTags[index].NodeName, "");
				}

				// Port
				char portNumberText[10] = "";
				cJSON* jsonPort = cJSON_GetObjectItem(jsonNext, "COM");
				if(jsonPort != NULL)
				{
					strcpy(portNumberText, jsonPort->valuestring);
				}
				else
				{
					strcpy(portNumberText, "0");
				}
				INI_data.pTags[index].PortNumber = atoi(portNumberText);


				// Device
				cJSON* jsonDevice = cJSON_GetObjectItem(jsonNext, "DEVNM");
				if(jsonDevice != NULL)
				{
					strcpy(INI_data.pTags[index].DeviceName, jsonDevice->valuestring);
				}
				else
				{
					strcpy(portNumberText, "");
				}

				// Readonly
				cJSON* jsonReadonly = cJSON_GetObjectItem(jsonNext, "SECL");
				if(jsonReadonly != NULL && jsonReadonly->valuestring != NULL)
				{
					INI_data.pTags[index].Readonly = strcmp(jsonReadonly->valuestring, "-1") == 0 ? IoT_READONLY : IoT_READWRITE;
				}
				else
				{
					INI_data.pTags[index].Readonly = IoT_READONLY;
				}
			}

			// Type
			cJSON* jsonType = cJSON_GetObjectItem(jsonNext, "TYPE");
			if(jsonType != NULL)
			{
				if (strcmp(jsonType->valuestring, "ANALOG") == 0)
				{
					INI_data.pTags[index].TagType = TAG_TYPE_ANALOG;
				}
				else if (strcmp(jsonType->valuestring, "DIGITAL") == 0)
				{
					INI_data.pTags[index].TagType = TAG_TYPE_DIGITAL;
				}
				else if (strcmp(jsonType->valuestring, "TEXT") == 0)
				{
					INI_data.pTags[index].TagType = TAG_TYPE_TEXT;
				}
				else
				{
					INI_data.pTags[index].TagType = 0;
				}
			}
			else
			{
				WASCADAHLog(Error, "\r\ParseTagValueText: Parse failed, no TYPE in JSON string: %s\r\n\n", jsonTagDetail);
			}

			//printf("\r\nParseTagDetail: index=%d, Project=%s, Node=%s, Port=%d, Device=%s, Name=%s\r\n\n",
			//	index, INI_data.pTags[index].ProjectName, INI_data.pTags[index].NodeName, INI_data.pTags[index].PortNumber, INI_data.pTags[index].DeviceName, INI_data.pTags[index].TagName);
		}
	}
	else
	{
		WASCADAHLog(Error, "\r\ParseTagValueText: Parse failed, no Tags in JSON string: %s\r\n\n", jsonTagDetail);
	}

	if(root != NULL)
	{
		cJSON_Delete(root);
	}
	return true;
}

/*Create Capability Message Structure to describe sensor data*/
MSG_CLASSIFY_T * CreateCapability()
{
	///*
	MSG_CLASSIFY_T* myCapability;
	MSG_CLASSIFY_T* devCap;
	MSG_ATTRIBUTE_T* attr;
	IoT_READWRITE_MODE mode = IoT_READONLY;

	myCapability = IoT_CreateRoot((char*)strHandlerName);

	///*
	devCap = IoT_AddGroup(myCapability, "Plugin");
	if (devCap)
	{
		mode = IoT_READONLY;
		attr = IoT_AddSensorNode(devCap, "Version");
		if (attr)
			IoT_SetStringValue(attr, strPluginVersion, mode);

		attr = IoT_AddSensorNode(devCap, "Description");
		if (attr)
			IoT_SetStringValue(attr, strPluginDescription, mode);
	}
	//*/

	return myCapability;

	//*/

	/*
	char projectList[MAX_JSON_BUF_LEN];
	int size = 0;

	MSG_CLASSIFY_T* myCapability = IoT_CreateRoot((char*)strHandlerName);
	return myCapability;
	*/

	/*
	//WebAccess_Initailzation(INI_data.Uid, INI_Context.Password, INI_data.Server);
	WebAccess_Initailzation(INI_Context.Authorization, INI_Context.Server);
	if (WebAccess_GetProjectList(projectList, &size))
	{ 
		ParseProjectList(projectList);
		UpdateProjectList(myCapability);
	}
	*/

	/*
	char* projectList = (char *)GetProjectList();
	ParseProjectList(projectList);
	UpdateProjectList(myCapability);
	*/
}


static void RetrieveAllTagsProc(pHandler_context pHandlerContex)
{
	char projectList[MAX_JSON_BUF_LEN];
	int size = 0;

	int mInterval = pHandlerContex->interval * INI_data.RetrieveInterval_ms;

	if(!g_Capability)
	{
		g_Capability = CreateCapability();

		//WebAccess_Initailzation(INI_data.Uid, INI_data.Password, INI_data.Server);
		WebAccess_Initailzation(INI_data.Authorization, INI_data.Server);
		if (WebAccess_GetProjectList(projectList, &size))
		{ 
			if(ParseProjectList(projectList) == true)
			{
				UpdateProjectList(g_Capability);
			}
		}

		UpdateSpecificTagsDetail(g_Capability);

		HandlerKernel_SetCapability(g_Capability, true);
	}

	while(pHandlerContex->isThreadRunning)
	{
		if(g_Capability)
		{
			if (WebAccess_GetProjectList(projectList, &size))
			{
				if(ParseProjectList(projectList) == true)
				{
					bool sync = true; //CheckProjectSync(g_Capability);
					if(sync == true)
					{
						UpdateSpecificTags(g_Capability);
					}
					else
					{
						IoT_ReleaseAll(g_Capability);
						g_Capability = CreateCapability();
						InitTags();
						UpdateProjectList(g_Capability);
					}
					HandlerKernel_SetCapability(g_Capability, true);
				}
			}
			/*
			char* projectList = (char *)GetProjectList();
			ParseProjectList(projectList);
			UpdateProjectList(g_Capability);
			*/
		}
		Sleep(mInterval);
	}
}


bool UpdateSpecificTags(MSG_CLASSIFY_T *parentGroup)
{
	char jsonTagValueLocal[MAX_JSON_BUF_LEN];
	char jsonTagDetailLocal[MAX_JSON_BUF_LEN];
	int size = 0;
	char *projectName = NULL;
	char *nodeName = NULL;
	int portNumber = 0;
	char *deviceName = NULL;
	char *tagName = NULL;

	for(int i=0; i<INI_data.numberOfTags; i++)
	{
		if(INI_data.pTags[i].ProjectName[0] != 0 && INI_data.pTags[i].TagName[0] != 0)
		{
			if(INI_data.pTags[i].NodeName[0] !=0 && INI_data.pTags[i].PortNumber > 0 && INI_data.pTags[i].DeviceName[0] != 0)
			{

#if defined TAG_FULL_PATH_REPORT 

				MSG_CLASSIFY_T *projectGroup = IoT_FindGroup(parentGroup, INI_data.pTags[i].ProjectName);
				if (projectGroup == NULL)
				{
					projectGroup = IoT_AddGroup(parentGroup, INI_data.pTags[i].ProjectName);
				}

				// Node
				MSG_CLASSIFY_T *nodeGroup = IoT_FindGroup(projectGroup, INI_data.pTags[i].NodeName);
				if (nodeGroup == NULL)
				{
					nodeGroup = IoT_AddGroup(projectGroup, INI_data.pTags[i].NodeName);
				}

				// Port
				char strPortNumber[30];
				itoa(INI_data.pTags[i].PortNumber, strPortNumber, 10);

				MSG_CLASSIFY_T *portGroup = IoT_FindGroup(nodeGroup, strPortNumber);
				if (portGroup == NULL)
				{
					portGroup = IoT_AddGroup(nodeGroup, strPortNumber);
				}

				// Device
				MSG_CLASSIFY_T *tempGroup = NULL;
				if (strlen(INI_data.pTags[i].DeviceName) != 0)
				{
					MSG_CLASSIFY_T *deviceGroup = IoT_FindGroup(portGroup, INI_data.pTags[i].DeviceName);
					if (deviceGroup == NULL)
					{
						deviceGroup = IoT_AddGroup(portGroup, INI_data.pTags[i].DeviceName);
					}
					tempGroup = deviceGroup;
				}
				else
				{
					tempGroup = nodeGroup;
				}

				WASCADAHLog(Debug, "Retrieve: Index=%d, Project=%s, Node=%s, Port=%d, Device=%s, Tag=%s", 
					i, 
					INI_data.pTags[i].ProjectName,
					INI_data.pTags[i].NodeName,
					INI_data.pTags[i].PortNumber,
					INI_data.pTags[i].DeviceName,
					INI_data.pTags[i].TagName);


				// Value
				if (INI_data.pTags[i].TagType == TAG_TYPE_TEXT)
				{
					if (WebAccess_GetTagValueText(jsonTagValueLocal, &size, INI_data.pTags[i].ProjectName, INI_data.pTags[i].TagName))
					{
						ParseTagValueText(jsonTagValueLocal, i);
						UpdateTagValueByIndex(tempGroup, i);
					}
				}
				else
				{
					if (WebAccess_GetTagValue(jsonTagValueLocal, &size, INI_data.pTags[i].ProjectName, INI_data.pTags[i].TagName))
					{
						ParseTagValue(jsonTagValueLocal, i);
						UpdateTagValueByIndex(tempGroup, i);
					}
				}


#else

				MSG_CLASSIFY_T *projectGroup = IoT_FindGroup(parentGroup, INI_data.pTags[i].ProjectName);
				if (projectGroup == NULL)
				{
					projectGroup = IoT_AddGroup(parentGroup, INI_data.pTags[i].ProjectName);
				}

				if (WebAccess_GetTagValue(jsonTagValueLocal, &size, INI_data.pTags[i].ProjectName, INI_data.pTags[i].TagName))
				{
					ParseTagValue(jsonTagValueLocal, i);
					UpdateTagValueByIndex(projectGroup, i);
				}
#endif

			}
			else
			{
				/*
				MSG_CLASSIFY_T *projectGroup = IoT_FindGroup(parentGroup, INI_data.pTags[i].ProjectName);
				if (projectGroup == NULL)
				{
					projectGroup = IoT_AddGroup(parentGroup, INI_data.pTags[i].ProjectName);
				}

				MSG_ATTRIBUTE_T* sensor = IoT_FindSensorNode(projectGroup, INI_data.pTags[i].TagName);
				if(sensor == NULL)
				{
					sensor = IoT_AddSensorNode(projectGroup, INI_data.pTags[i].TagName);
				}
				IoT_SetStringValue(sensor, "Unavailable", IoT_READONLY);
				*/

				MSG_CLASSIFY_T *projectGroup = IoT_FindGroup(parentGroup, INI_data.pTags[i].ProjectName);
				if (projectGroup == NULL)
				{
					projectGroup = IoT_AddGroup(parentGroup, INI_data.pTags[i].ProjectName);
				}
				ReportTagUnavailableByIndex(projectGroup, i);

				HandlerKernel_SetCapability(g_Capability, true);

			}

			if(INI_data.ResyncCapability == true)
			{
				HandlerKernel_SetCapability(g_Capability, true);
				INI_data.ResyncCapability = false;
			}

		}

		Sleep(50);
	}
	return true;
}

bool UpdateSpecificTagsDetail(MSG_CLASSIFY_T *parentGroup)
{
	char jsonTagValueLocal[MAX_JSON_BUF_LEN];
	char jsonTagDetailLocal[MAX_JSON_BUF_LEN];
	int size = 0;
	char *projectName = NULL;
	char *nodeName = NULL;
	int portNumber = 0;
	char *deviceName = NULL;
	char *tagName = NULL;
	int retryTimes = 0;

	for (int i = 0; i<INI_data.numberOfTags; i++)
	{
		if (INI_data.pTags[i].ProjectName[0] != 0 && INI_data.pTags[i].TagName[0] != 0)
		{
			if (WebAccess_GetTagDetail(jsonTagDetailLocal, &size, INI_data.pTags[i].ProjectName, INI_data.pTags[i].TagName, "ALL"))
			{
				//printf("\r\nGetTagDetail for %s: %s\r\n\n", INI_data.pTags[i].TagName, jsonTagDetailLocal);

				if (ParseTagDetail(jsonTagDetailLocal, i, false))
				{
#if defined TAG_FULL_PATH_REPORT

					if((strcmp(INI_data.pTags[i].NodeName, "") != 0) && (INI_data.pTags[i].PortNumber > 0) && (strcmp(INI_data.pTags[i].DeviceName, "") != 0))
					{
						// Get Tag Detail & parse successful
						INI_data.pTags[i].TagAvailable = true;

						// Project
						MSG_CLASSIFY_T *projectGroup = IoT_FindGroup(parentGroup, INI_data.pTags[i].ProjectName);
						if (projectGroup == NULL)
						{
							projectGroup = IoT_AddGroup(projectGroup, INI_data.pTags[i].ProjectName);
						}

						// Node
						MSG_CLASSIFY_T *nodeGroup = IoT_FindGroup(projectGroup, INI_data.pTags[i].NodeName);
						if (nodeGroup == NULL)
						{
							nodeGroup = IoT_AddGroup(projectGroup, INI_data.pTags[i].NodeName);
						}

						// Port
						char strPortNumber[30];
						itoa(INI_data.pTags[i].PortNumber, strPortNumber, 10);

						MSG_CLASSIFY_T *portGroup = IoT_FindGroup(nodeGroup, strPortNumber);
						if (portGroup == NULL)
						{
							portGroup = IoT_AddGroup(nodeGroup, strPortNumber);
						}

						// Device
						MSG_CLASSIFY_T *tempGroup = NULL;
						if (strlen(INI_data.pTags[i].DeviceName) != 0)
						{
							MSG_CLASSIFY_T *deviceGroup = IoT_FindGroup(portGroup, INI_data.pTags[i].DeviceName);
							if (deviceGroup == NULL)
							{
								deviceGroup = IoT_AddGroup(portGroup, INI_data.pTags[i].DeviceName);
							}

							tempGroup = deviceGroup;
						}
						else
						{
							tempGroup = nodeGroup;
						}

						//printf("UpdateSpecificTagsDetail: tempGroup=%s\r\n", IoT_PrintCapability(tempGroup));
						WASCADAHLog(Debug, "Getting Tag Value: Index=%d, Project=%s, Node=%s, Port=%d, Device=%s, Tag=%s", 
							i, 
							INI_data.pTags[i].ProjectName,
							INI_data.pTags[i].NodeName,
							INI_data.pTags[i].PortNumber,
							INI_data.pTags[i].DeviceName,
							INI_data.pTags[i].TagName);


						// Value
						if (INI_data.pTags[i].TagType == TAG_TYPE_TEXT)
						{
							if (WebAccess_GetTagValueText(jsonTagValueLocal, &size, INI_data.pTags[i].ProjectName, INI_data.pTags[i].TagName))
							{
								ParseTagValueText(jsonTagValueLocal, i);
								UpdateTagValueByIndex(tempGroup, i);
							}
						}
						else
						{
							if (WebAccess_GetTagValue(jsonTagValueLocal, &size, INI_data.pTags[i].ProjectName, INI_data.pTags[i].TagName))
							{
								ParseTagValue(jsonTagValueLocal, i);
								UpdateTagValueByIndex(tempGroup, i);
							}
						}
					}
					else
					{
						INI_data.pTags[i].TagAvailable = false;
					}
				

#else
					MSG_CLASSIFY_T *projectGroup = IoT_FindGroup(parentGroup, INI_data.pTags[i].ProjectName);
					if (projectGroup == NULL)
					{
						projectGroup = IoT_AddGroup(projectGroup, INI_data.pTags[i].ProjectName);
					}

					if (INI_data.pTags[i].TagType == TAG_TYPE_TEXT)
					{
						if (WebAccess_GetTagValueText(jsonTagValueLocal, &size, INI_data.pTags[i].ProjectName, INI_data.pTags[i].TagName))
						{
							ParseTagValueText(jsonTagValueLocal, i);
							UpdateTagValueByIndex(projectGroup, i);
						}
					}
					else
					{
						if (WebAccess_GetTagValue(jsonTagValueLocal, &size, INI_data.pTags[i].ProjectName, INI_data.pTags[i].TagName))
						{
							ParseTagValue(jsonTagValueLocal, i);
							UpdateTagValueByIndex(projectGroup, i);
						}
					}
#endif

				}
				else
				{
					// Parse TagDetail fail ...
					INI_data.pTags[i].TagAvailable = false;
				}


				Sleep(10);
				retryTimes = 0;
			}
			else
			{
				if(retryTimes < 3)
				{
					WASCADAHLog(Warning, "UpdateSpecificTagsDetail: Retry to get [%s] detail info, retry loop %d ...", INI_data.pTags[i].TagName, retryTimes);
					retryTimes++;
					i--;
				}
				else
				{
					WASCADAHLog(Error, "\r\nUpdateSpecificTagsDetail: Get [%s] detail info fail!!\r\n\n", INI_data.pTags[i].TagName);
					retryTimes = 0;
					INI_data.pTags[i].TagAvailable = false;
				}

			}
		}
	}
	return true;
}


bool UpdateSpecificProjects(MSG_CLASSIFY_T *parentGroup)
{
	for(int i=0; i<INI_data.numberOfTags; i++)
	{
		if(INI_data.pTags[i].ProjectName[0] != 0)
		{
			MSG_CLASSIFY_T *projectGroup = IoT_FindGroup(parentGroup, INI_data.pTags[i].ProjectName);
			if(projectGroup == NULL)
			{
				projectGroup =  IoT_AddGroup(parentGroup, INI_data.pTags[i].ProjectName);
			}
		}
	}
	return true;
}


static void RetrieveSpecificTagsProc(pHandler_context pHandlerContex)
{
	int mInterval = pHandlerContex->interval * INI_data.RetrieveInterval_ms;

	if(!g_Capability)
	{
		g_Capability = CreateCapability();
		//WebAccess_Initailzation(INI_data.Uid, INI_data.Password, INI_data.Server);
		WebAccess_Initailzation(INI_data.Authorization, INI_data.Server);

		UpdateSpecificProjects(g_Capability);
		UpdateSpecificTagsDetail(g_Capability);


		WASCADAHLog(Normal, "\r\nSetCapability: %s\r\n", IoT_PrintCapability(g_Capability));

		HandlerKernel_SetCapability(g_Capability, true);
	}

	while(pHandlerContex->isThreadRunning)
	{
		if(g_Capability)
		{
			UpdateSpecificTags(g_Capability);
		}
		Sleep(mInterval);
	}
}




static void RetrieveTagDetailProc(pHandler_context pHandlerContex)
{
	char jsonTagValueLocal[MAX_JSON_BUF_LEN];
	char jsonTagDetailLocal[MAX_JSON_BUF_LEN];
	int UnavailableCount = 0;

	while(pHandlerContex->isThreadRunningGetTagDetail)
	{
		int size = 0;
		INI_data.numberOfUnavailableTags = 0;

		// Count the number of unavailable Tags
		for(int i=0; i<INI_data.numberOfTags; i++)
		{
			if ((INI_data.pTags[i].TagAvailable == false) && (INI_data.pTags[i].ProjectName[0] != 0) && (INI_data.pTags[i].TagName[0] != 0))
			{
				WASCADAHLog(Warning, "\r\nProcessing uncompleted Tag: Index=%d, Project=%s, Tag=%s\r\n",
					i,
					INI_data.pTags[i].ProjectName,
					INI_data.pTags[i].TagName);

				INI_data.numberOfUnavailableTags++;
			}
		}

		if(INI_data.numberOfUnavailableTags == 0)
		{
			//pHandlerContex->isThreadRunningGetTagDetail = false;
			//break;
		}


		// Retrieve Tag Detail
		for(int i=0; i<INI_data.numberOfTags; i++)
		{

			if((INI_data.pTags[i].ProjectName[0] == 0) || (INI_data.pTags[i].TagName[0] == 0))
			{
				continue;
			}

			if ((INI_data.pTags[i].TagAvailable == false || INI_data.pTags[i].TagValue == -2))
			{
				if (WebAccess_GetTagDetail(jsonTagDetailLocal, &size, INI_data.pTags[i].ProjectName, INI_data.pTags[i].TagName, "ALL"))
				{
					if (ParseTagDetail(jsonTagDetailLocal, i, false))
					{
						// Find and add new tag into g_Capability.

						if((strcmp(INI_data.pTags[i].NodeName, "") != 0) && (INI_data.pTags[i].PortNumber > 0) && (strcmp(INI_data.pTags[i].DeviceName, "") != 0))
						{
							// Get Tag Detail & parse successful
							INI_data.pTags[i].TagAvailable = true;

							// Project
							MSG_CLASSIFY_T *projectGroup = IoT_FindGroup(g_Capability, INI_data.pTags[i].ProjectName);
							if (projectGroup == NULL)
							{
								projectGroup = IoT_AddGroup(projectGroup, INI_data.pTags[i].ProjectName);
							}

							// Node
							MSG_CLASSIFY_T *nodeGroup = IoT_FindGroup(projectGroup, INI_data.pTags[i].NodeName);
							if (nodeGroup == NULL)
							{
								nodeGroup = IoT_AddGroup(projectGroup, INI_data.pTags[i].NodeName);
							}

							// Port
							char strPortNumber[30];
							itoa(INI_data.pTags[i].PortNumber, strPortNumber, 10);

							MSG_CLASSIFY_T *portGroup = IoT_FindGroup(nodeGroup, strPortNumber);
							if (portGroup == NULL)
							{
								portGroup = IoT_AddGroup(nodeGroup, strPortNumber);
							}

							// Device
							MSG_CLASSIFY_T *tempGroup = NULL;
							if (strlen(INI_data.pTags[i].DeviceName) != 0)
							{
								MSG_CLASSIFY_T *deviceGroup = IoT_FindGroup(portGroup, INI_data.pTags[i].DeviceName);
								if (deviceGroup == NULL)
								{
									deviceGroup = IoT_AddGroup(portGroup, INI_data.pTags[i].DeviceName);
								}

								tempGroup = deviceGroup;
							}
							else
							{
								tempGroup = nodeGroup;
							}

							//printf("UpdateSpecificTagsDetail: tempGroup=%s\r\n", IoT_PrintCapability(tempGroup));
							WASCADAHLog(Debug, "\r\nGetting Tag Detail Successful: Index=%d, Project=%s, Node=%s, Port=%d, Device=%s, Tag=%s\r\n",
								i, 
								INI_data.pTags[i].ProjectName,
								INI_data.pTags[i].NodeName,
								INI_data.pTags[i].PortNumber,
								INI_data.pTags[i].DeviceName,
								INI_data.pTags[i].TagName);


							// Value
							if (INI_data.pTags[i].TagType == TAG_TYPE_TEXT)
							{
								if (WebAccess_GetTagValueText(jsonTagValueLocal, &size, INI_data.pTags[i].ProjectName, INI_data.pTags[i].TagName))
								{
									ParseTagValueText(jsonTagValueLocal, i);
									UpdateTagValueByIndex(tempGroup, i);
								}
							}
							else
							{
								if (WebAccess_GetTagValue(jsonTagValueLocal, &size, INI_data.pTags[i].ProjectName, INI_data.pTags[i].TagName))
								{
									ParseTagValue(jsonTagValueLocal, i);
									UpdateTagValueByIndex(tempGroup, i);
								}
							}

							// Check if Tag Detail completed ?
							if ((INI_data.pTags[i].ProjectName[0] != 0) && 
								(INI_data.pTags[i].NodeName[0] != 0) && 
								(INI_data.pTags[i].PortNumber > 0) &&
								(INI_data.pTags[i].DeviceName[0] != 0) && 
								(INI_data.pTags[i].TagName[0] != 0))
							{
								INI_data.pTags[i].TagAvailable = true;

								MSG_ATTRIBUTE_T* sensor = IoT_FindSensorNode(projectGroup, INI_data.pTags[i].TagName);
								if(sensor != NULL)
								{
									IoT_DelSensorNode(projectGroup, INI_data.pTags[i].TagName);
								}

								HandlerKernel_SetCapability(g_Capability, true);
							}
						}
						else
						{
							char message[128] = { "" };
							char description[128] = { "" };

							// Event message
							sprintf(message, "TAG_INFO_UNAVAILABLE", INI_data.pTags[i].TagName);
							// Event description
							sprintf(description, "Tag's detail information is unavailable: Index=%d, Project=%s, Tag=%s",
								i,
								INI_data.pTags[i].ProjectName,
								INI_data.pTags[i].TagName);
							PrepareEvents("WASCADA10001", "WEBACCESS_SCADA_TAG_ERROR", message, description, Severity_Error);

							// Tag Detail Information is unavailable.
							WASCADAHLog(Error, "\r\nTag's detail information is unavailable: Index=%d, Project=%s, Tag=%s\r\n",
								i,
								INI_data.pTags[i].ProjectName,
								INI_data.pTags[i].TagName);


							// Project
							MSG_CLASSIFY_T *projectGroup = IoT_FindGroup(g_Capability, INI_data.pTags[i].ProjectName);
							if (projectGroup == NULL)
							{
								projectGroup = IoT_AddGroup(projectGroup, INI_data.pTags[i].ProjectName);
							}

							ReportTagUnavailableByIndex(projectGroup, i);
						
						}
					}
					else
					{
						// Todo: Need to do recursive search to find deleted tag.
						/*
						MSG_CLASSIFY_T *targetGroup = IoT_FindGroup(g_Capability, INI_data.pTags[i].ProjectName);
						while(targetGroup != NULL)
						{
							
							MSG_ATTRIBUTE_T* targetSensor = IoT_FindSensorNode(targetGroup, INI_data.pTags[i].TagName);
							if(targetSensor != NULL)
							{

								INI_data.pTags[i].TagAvailable = false;
								IoT_DelSensorNode(targetGroup, INI_data.pTags[i].TagName);
								HandlerKernel_SetCapability(g_Capability, false);
								INI_data.ResyncCapability = true;

								WASCADAHLog(Warning, "\r\nRemoved unavailable Tag: Index=%d, Project=%s, Node=%s, Port=%d, Device=%s, Tag=%s\r\n",
									i,
									INI_data.pTags[i].ProjectName,
									INI_data.pTags[i].NodeName,
									INI_data.pTags[i].PortNumber,
									INI_data.pTags[i].DeviceName,
									INI_data.pTags[i].TagName);

								strcpy(INI_data.pTags[i].NodeName, "");
								INI_data.pTags[i].PortNumber = 0;
								strcpy(INI_data.pTags[i].DeviceName, "");
								strcpy(INI_data.pTags[i].TagValueText, "Unavailable");

								break;
							}
							else
							{
								targetGroup = targetGroup->next;
							}
						}
						*/


						///*
						// Find and remove deleted tag from g_Capability.
						// Project
						MSG_CLASSIFY_T *projectGroup = IoT_FindGroup(g_Capability, INI_data.pTags[i].ProjectName);
						if (projectGroup == NULL)
						{
							//projectGroup = IoT_AddGroup(projectGroup, INI_data.pTags[i].ProjectName);
							break;
						}

						// Node
						MSG_CLASSIFY_T *nodeGroup = IoT_FindGroup(projectGroup, INI_data.pTags[i].NodeName);
						if (nodeGroup == NULL)
						{
							//nodeGroup = IoT_AddGroup(projectGroup, INI_data.pTags[i].NodeName);
							break;
						}

						// Port
						char strPortNumber[30];
						itoa(INI_data.pTags[i].PortNumber, strPortNumber, 10);

						MSG_CLASSIFY_T *portGroup = IoT_FindGroup(nodeGroup, strPortNumber);
						if (portGroup == NULL)
						{
							//portGroup = IoT_AddGroup(nodeGroup, strPortNumber);
							break;
						}

						// Device
						MSG_CLASSIFY_T *tempGroup = NULL;
						if (strlen(INI_data.pTags[i].DeviceName) != 0)
						{
							MSG_CLASSIFY_T *deviceGroup = IoT_FindGroup(portGroup, INI_data.pTags[i].DeviceName);
							if (deviceGroup == NULL)
							{
								//deviceGroup = IoT_AddGroup(portGroup, INI_data.pTags[i].DeviceName);
								break;
							}

							tempGroup = deviceGroup;
						}
						else
						{
							tempGroup = nodeGroup;
						}

						MSG_ATTRIBUTE_T* sensor = IoT_FindSensorNode(tempGroup, INI_data.pTags[i].TagName);
						if(sensor == NULL)
						{
							break;
						}
						else
						{
							if(1)
							{
								INI_data.pTags[i].TagAvailable = false;
								IoT_DelSensorNode(tempGroup, INI_data.pTags[i].TagName);
								HandlerKernel_SetCapability(g_Capability, false);
								INI_data.ResyncCapability = true;

								WASCADAHLog(Warning, "\r\nRearrange unavailable Tag: Index=%d, Project=%s, Node=%s, Port=%d, Device=%s, Tag=%s\r\n",
									i,
									INI_data.pTags[i].ProjectName,
									INI_data.pTags[i].NodeName,
									INI_data.pTags[i].PortNumber,
									INI_data.pTags[i].DeviceName,
									INI_data.pTags[i].TagName);

								strcpy(INI_data.pTags[i].NodeName, "");
								INI_data.pTags[i].PortNumber = 0;
								strcpy(INI_data.pTags[i].DeviceName, "");
								strcpy(INI_data.pTags[i].TagValueText, "Unavailable");
							}
							else
							{
								INI_data.pTags[i].TagAvailable = false;
								HandlerKernel_SetCapability(g_Capability, false);
								INI_data.ResyncCapability = true;

								WASCADAHLog(Warning, "\r\nFound removed Tag: Index=%d, Project=%s, Node=%s, Port=%d, Device=%s, Tag=%s\r\n",
									i,
									INI_data.pTags[i].ProjectName,
									INI_data.pTags[i].NodeName,
									INI_data.pTags[i].PortNumber,
									INI_data.pTags[i].DeviceName,
									INI_data.pTags[i].TagName);

								strcpy(INI_data.pTags[i].NodeName, "");
								INI_data.pTags[i].PortNumber = 0;
								strcpy(INI_data.pTags[i].DeviceName, "");
								strcpy(INI_data.pTags[i].TagValueText, "Unavailable");

							}
						}
						//*/
					}
				}
				else
				{
					char message[128] = { "" };
					char description[128] = { "" };

					// Event message
					sprintf(message, "TAG_INFO_UNAVAILABLE", INI_data.pTags[i].TagName);
					// Event description
					sprintf(description, "Get Tag's detail information failed: Index=%d, Project=%s, Tag=%s", 						i,
						INI_data.pTags[i].ProjectName,
						INI_data.pTags[i].TagName);
					PrepareEvents("WASCADA10002", "WEBACCESS_SCADA_TAG_ERROR", message, description, Severity_Error);

					WASCADAHLog(Error, "\r\nGet Tag's detail information failed: Index=%d, Project=%s, Tag=%s\r\n",
						i,
						INI_data.pTags[i].ProjectName,
						INI_data.pTags[i].TagName);


					// Add unavailable Tag to Project
					MSG_CLASSIFY_T *projectGroup = IoT_FindGroup(g_Capability, INI_data.pTags[i].ProjectName);
					if (projectGroup == NULL)
					{
						projectGroup = IoT_AddGroup(projectGroup, INI_data.pTags[i].ProjectName);
					}
					ReportTagUnavailableByIndex(projectGroup, i);
				}
			}
		}
		Sleep(INI_data.PollingTagInterval_sec*1000);
	}
}




static DWORD WINAPI WASCANDAHandlerReportThread(void *args)
{
	if(INI_data.RetrieveSpecificTags == true)
	{
		RetrieveSpecificTagsProc((pHandler_context)args);
	}
	else
	{
		RetrieveAllTagsProc((pHandler_context)args);
	}
    return 0;
}


static DWORD WINAPI WASCANDAHandlerGetTagDetailThread(void *args)
{
	RetrieveTagDetailProc((pHandler_context)args);
    return 0;
}



bool FindProjectName(char* str_search, char* project_name)
{
	char* p_str1;
	char* p_str2;
	char str_pattern1[20] = { '\0' };
	char str_pattern2 = '/';
	int size_str = 0;

	sprintf_s(str_pattern1, sizeof(str_pattern1), "%s/", strHandlerName);
	p_str1 = strstr(str_search,str_pattern1);

	if (p_str1)
	{
		p_str1 += strlen(str_pattern1);
		p_str2 = strchr(p_str1,str_pattern2);
		size_str = p_str2 - p_str1;

		if (size_str > 0)
		{
			strncpy(project_name, p_str1, size_str);
		}
		else
		{
			return false;
		}
	}
	else
	{
		return false;
	}

	return true;
}


bool FindTagName(char* str_search, char* tag_name)
{
	char* tempName = strrchr(str_search, '/');

	if(tempName != NULL && strlen(tempName) > 1)
	{
		strcpy(tag_name, tempName+1);
		return true;
	}
	else
	{
		return false;
	}
}


/*callback function to handle threshold rule check event*/
void on_threshold_triggered(threshold_event_type type, char* sensorname, double value, MSG_ATTRIBUTE_T* attr, void *pRev)
{
	WASCADAHLog(Debug, " %s> threshold triggered:[%d, %s, %f]", g_HandlerInfo.Name, type, sensorname, value);
}


/*callback function to handle get sensor data event*/
bool on_get_sensor(get_data_t* objlist, void *pRev)
{
	char project_name[50] = {'\0'};
	char tag_name[50] = {'\0'};
	int tag_index = 0;

	get_data_t *current = objlist;

	if(objlist == NULL)
		return false;

	if (FindProjectName(current->sensorname, project_name)
		&& FindTagName(current->sensorname, tag_name))
	{
		if(strcmp(tag_name, "Version") == 0)
		{
			strcpy(current->attr->sv, strPluginVersion);
		}
		else if(strcmp(tag_name, "Description") == 0)
		{
			strcpy(current->attr->sv, strPluginDescription);
		}
		else
		{
			// To get most update tag data from WebAccess/SCADA.
			tag_index = UpdateTagValueByName(g_Capability, tag_name);

			switch(current->attr->type)
			{
			case attr_type_numeric:
				current->attr->v = INI_data.pTags[tag_index].TagValue;
			 break;

			case attr_type_boolean:
				current->attr->bv = INI_data.pTags[tag_index].TagValue;
			 break;

			case attr_type_string:
				strcpy(current->attr->sv, INI_data.pTags[tag_index].TagValueText);
			 break;
			}
		}
	}

	while(current)
	{
		current->errcode = STATUSCODE_SUCCESS;
		strcpy(current->errstring, STATUS_SUCCESS);

		switch(current->attr->type)
		{
		case attr_type_numeric:
			WASCADAHLog(Debug, " %s> get: %s value:%d", g_HandlerInfo.Name, current->sensorname, current->attr->v);
		 break;
		case attr_type_boolean:
			WASCADAHLog(Debug, " %s> get: %s value:%s", g_HandlerInfo.Name, current->sensorname, current->attr->bv?"true":"false");
		 break;
		case attr_type_string:
			WASCADAHLog(Debug, " %s> get: %s value:%s", g_HandlerInfo.Name, current->sensorname, current->attr->sv);
		 break;
		case attr_type_date:
			WASCADAHLog(Debug, " %s> get: %s value:Date:%s", g_HandlerInfo.Name, current->sensorname, current->attr->sv);
		 break;
		case attr_type_timestamp:
		 WASCADAHLog(Debug, " %s> get: %s value:Timestamp:%d", g_HandlerInfo.Name, current->sensorname, current->attr->v);
		 break;
		}

		current = current->next;
	}
	return true;
}


/*callback function to handle set sensor data event*/
bool on_set_sensor(set_data_t* objlist, void *pRev)
{
	bool ret = true;
	set_data_t *current = objlist;

	if(objlist == NULL)
		return false;

	while(current)
	{
		char jsonResult[MAX_JSON_BUF_LEN];
		int size = 0;
		char project_name[50] = {'\0'};
		char tag_name[50] = {'\0'};
		char value[50] = {'\0'};

		MSG_ATTRIBUTE_T* attr = IoT_FindSensorNodeWithPath(g_Capability, current->sensorname);
		current->errcode = STATUSCODE_SUCCESS;
		strcpy(current->errstring, STATUS_SUCCESS);

		if(attr != NULL)
		{
			if (FindProjectName(current->sensorname, project_name)
				&& FindTagName(current->sensorname, tag_name))
			{
				switch(current->newtype)
				{
					case attr_type_numeric:
						sprintf_s(value, sizeof(value), "%lf", current->v);
						if (!WebAccess_SetTagValue(jsonResult, &size, project_name, tag_name, value))
						{
							ret = false;
							WASCADAHLog(Debug, " %s> set: %s value:%d (numeric), failed", g_HandlerInfo.Name, current->sensorname, current->v);
						}
						break;

					case attr_type_boolean:
						sprintf_s(value, sizeof(value), "%d", current->bv);
						if (!WebAccess_SetTagValue(jsonResult, &size, project_name, tag_name, value))
						{
							ret = false;
							WASCADAHLog(Debug, " %s> set: %s value:%s (boolean), failed", g_HandlerInfo.Name, current->sensorname, current->bv?"true":"false");
						}
						break;

					case attr_type_string:
						sprintf_s(value, sizeof(value), "%s", current->sv);
						if (!WebAccess_SetTagValueText(jsonResult, &size, project_name, tag_name, value))
						{
							ret = false;
							WASCADAHLog(Debug, " %s> set: %s value:%s (string), failed", g_HandlerInfo.Name, current->sensorname, current->sv);
						}
						break;

					default:
						ret = false;
						break;
				}
			}
			// To get most update tag data from WebAccess/SCADA.
			UpdateTagValueByName(g_Capability, tag_name);
		}
		current = current->next;
	}
	return ret;
}


/* **************************************************************************************
 *  Function Name: Handler_Initialize
 *  Description: Init any objects or variables of this handler
 *  Input :  PLUGIN_INFO *pluginfo
 *  Output: None
 *  Return:  0  : Success Init Handler
 *              -1 : Fail Init Handler
 * ***************************************************************************************/
int HANDLER_API Handler_Initialize( HANDLER_INFO *pluginfo )
{
	char version[MAX_VERSION_LEN];
	char description[MAX_DESCRIPTION_LEN];

	if( pluginfo == NULL )
		return handler_fail;

	// 1. Topic of this handler
	strHandlerName = (char *)calloc(strlen(pluginfo->Name) + 1, sizeof(char));
	strcpy(strHandlerName, pluginfo->Name);

	/*
	strPluginName = (char *)calloc(strlen(pluginfo->Name) + 1, sizeof(char));	
	strcpy(strPluginName, pluginfo->Name);
	sprintf_s( strHandlerName, sizeof(strHandlerName), "%s", strPluginName );
	*/

	pluginfo->RequestID = cagent_request_custom;
	pluginfo->ActionID = cagent_custom_action;
	g_wascadahandlerlog = pluginfo->loghandle;
	WASCADAHLog(Debug, " %s> Initialize", strHandlerName);

	sprintf(version, "%d.%d.%d", VER_MAJOR, VER_MINOR, VER_BUILD);
	strPluginVersion = (char *)calloc(strlen(version) + 1, sizeof(char));
	strcpy(strPluginVersion, version);

	sprintf(description, "%s", SW_DESC);
	strPluginDescription = (char *)calloc(strlen(description) + 1, sizeof(char));
	strcpy(strPluginDescription, description);

	WASCADAHLog(Normal, " %s> Name: %s\r\n", strHandlerName, strHandlerName);
	WASCADAHLog(Normal, " %s> Version: %s\r\n", strHandlerName, strPluginVersion);
	WASCADAHLog(Normal, " %s> Description: %s\r\n", strHandlerName, strPluginDescription);

	// 2. Copy agent info 
	memcpy(&g_HandlerInfo, pluginfo, sizeof(HANDLER_INFO));
	g_HandlerInfo.agentInfo = pluginfo->agentInfo;

	// 3. Callback function -> Send JSON Data by this callback function
	g_HandlerContex.threadHandler = NULL;
	g_HandlerContex.isThreadRunning = false;

	g_HandlerContex.threadHandlerGetTagDetail = NULL;
	g_HandlerContex.isThreadRunningGetTagDetail = false;

	g_status = handler_status_no_init;
	g_sendeventcbf = g_HandlerInfo.sendeventcbf;

	return HandlerKernel_Initialize(pluginfo);
}

/* **************************************************************************************
 *  Function Name: Handler_Uninitialize
 *  Description: Release the objects or variables used in this handler
 *  Input :  None
 *  Output: None
 *  Return:  void
 * ***************************************************************************************/
void Handler_Uninitialize()
{
	/*Stop read text file thread*/
	if(g_HandlerContex.threadHandlerGetTagDetail)
	{
		g_HandlerContex.isThreadRunningGetTagDetail = false;
		WaitForSingleObject(g_HandlerContex.threadHandlerGetTagDetail, INFINITE);
		CloseHandle(g_HandlerContex.threadHandlerGetTagDetail);
		g_HandlerContex.threadHandlerGetTagDetail = NULL;
	}

	if(g_HandlerContex.threadHandler)
	{
		g_HandlerContex.isThreadRunning = false;
		WaitForSingleObject(g_HandlerContex.threadHandler, INFINITE);
		CloseHandle(g_HandlerContex.threadHandler);
		g_HandlerContex.threadHandler = NULL;
	}

	HandlerKernel_Uninitialize();
	/*Release Capability Message Structure*/
	if(g_Capability)
	{
		IoT_ReleaseAll(g_Capability);
		g_Capability = NULL;
	}
}

/* **************************************************************************************
 *  Function Name: Handler_Get_Status
 *  Description: Get Handler Threads Status. CAgent will restart current Handler or restart CAgent self if busy.
 *  Input :  None
 *  Output: char * : pOutStatus       // cagent handler status
 *  Return:  handler_success  : Success Init Handler
 *			 handler_fail : Fail Init Handler
 * **************************************************************************************/
int HANDLER_API Handler_Get_Status( HANDLER_THREAD_STATUS * pOutStatus )
{
	int iRet = handler_fail; 
	WASCADAHLog(Debug, " %s> Get Status", strHandlerName);
	if(!pOutStatus) return iRet;
	/*user need to implement their thread status check function*/
	*pOutStatus = g_status;
	
	iRet = handler_success;
	return iRet;
}


/* **************************************************************************************
 *  Function Name: Handler_OnStatusChange
 *  Description: Agent can notify handler the status is changed.
 *  Input :  PLUGIN_INFO *pluginfo
 *  Output: None
 *  Return:  None
 * ***************************************************************************************/
void HANDLER_API Handler_OnStatusChange( HANDLER_INFO *pluginfo )
{
	WASCADAHLog(Debug, " %s> Update Status", strHandlerName);
	if(pluginfo)
		memcpy(&g_HandlerInfo, pluginfo, sizeof(HANDLER_INFO));
	else
	{
		memset(&g_HandlerInfo, 0, sizeof(HANDLER_INFO));
		sprintf_s( g_HandlerInfo.Name, sizeof( g_HandlerInfo.Name), "%s", strHandlerName );
		g_HandlerInfo.RequestID = cagent_request_custom;
		g_HandlerInfo.ActionID = cagent_custom_action;
	}
}

/* **************************************************************************************
 *  Function Name: Handler_Start
 *  Description: Start Running
 *  Input :  None
 *  Output: None
 *  Return:  0  : Success Init Handler
 *              -1 : Fail Init Handler
 * ***************************************************************************************/
int HANDLER_API Handler_Start( void )
{
	read_INI();


	WASCADAHLog(Debug, "> %s Start", strHandlerName);
	/*Create thread to read text file*/
	g_HandlerContex.interval = 1;
	g_HandlerContex.isThreadRunning = true;
	g_HandlerContex.threadHandler = CreateThread(NULL, 0, WASCANDAHandlerReportThread, &g_HandlerContex, 0, NULL);

	g_HandlerContex.isThreadRunningGetTagDetail = true;
	g_HandlerContex.threadHandlerGetTagDetail = CreateThread(NULL, 0, WASCANDAHandlerGetTagDetailThread, &g_HandlerContex, 0, NULL);

	g_status = handler_status_start;
	return handler_success;
}

/* **************************************************************************************
 *  Function Name: Handler_Stop
 *  Description: Stop the handler
 *  Input :  None
 *  Output: None
 *  Return:  0  : Success Init Handler
 *              -1 : Fail Init Handler
 * ***************************************************************************************/
int HANDLER_API Handler_Stop( void )
{
	WASCADAHLog(Debug, "> %s Stop", strHandlerName);

	/*Stop text file read thread GetTagDetail*/
	if(g_HandlerContex.threadHandlerGetTagDetail)
	{
		g_HandlerContex.isThreadRunningGetTagDetail = false;
		WaitForSingleObject(g_HandlerContex.threadHandlerGetTagDetail, INFINITE);
		CloseHandle(g_HandlerContex.threadHandlerGetTagDetail);
		g_HandlerContex.threadHandlerGetTagDetail = NULL;
	}

	/*Stop text file read thread*/
	if(g_HandlerContex.threadHandler)
	{
		g_HandlerContex.isThreadRunning = false;
		WaitForSingleObject(g_HandlerContex.threadHandler, INFINITE);
		CloseHandle(g_HandlerContex.threadHandler);
		g_HandlerContex.threadHandler = NULL;
	}

	g_status = handler_status_stop;
	return handler_success;
}

/* **************************************************************************************
 *  Function Name: Handler_Recv
 *  Description: Receive Packet from MQTT Server
 *  Input : char * const topic, 
 *			void* const data, 
 *			const size_t datalen
 *  Output: void *pRev1, 
 *			void* pRev2
 *  Return: None
 * ***************************************************************************************/
void HANDLER_API Handler_Recv(char * const topic, void* const data, const size_t datalen, void *pRev1, void* pRev2  )
{
	int cmdID = 0;
	char sessionID[33] = {0};
	printf(" >Recv Topic [%s] Data %s\n", topic, (char*) data );
	
	/*Parse Received Command*/
	if(HandlerKernel_ParseRecvCMDWithSessionID((char*)data, &cmdID, sessionID) != handler_success)
		return;
	switch(cmdID)
	{
	case hk_auto_upload_req:
		/*start live report*/
		HandlerKernel_LiveReportStart(hk_auto_upload_rep, (char*)data);
		break;
	case hk_set_thr_req:
		/*Stop threshold check thread*/
		HandlerKernel_StopThresholdCheck();
		/*setup threshold rule*/
		HandlerKernel_SetThreshold(hk_set_thr_rep,(char*) data);
		/*register the threshold check callback function to handle trigger event*/
		HandlerKernel_SetThresholdTrigger(on_threshold_triggered);
		/*Restart threshold check thread*/
		HandlerKernel_StartThresholdCheck();
		break;
	case hk_del_thr_req:
		/*Stop threshold check thread*/
		HandlerKernel_StopThresholdCheck();
		/*clear threshold check callback function*/
		HandlerKernel_SetThresholdTrigger(NULL);
		/*Delete all threshold rules*/
		HandlerKernel_DeleteAllThreshold(hk_del_thr_rep);
		break;
	case hk_get_sensors_data_req:
		/*Get Sensor Data with callback function*/
		HandlerKernel_GetSensorData(hk_get_sensors_data_rep, sessionID, (char*)data, on_get_sensor);
		break;
	case hk_set_sensors_data_req:
		/*Set Sensor Data with callback function*/
		HandlerKernel_SetSensorData(hk_set_sensors_data_rep, sessionID, (char*)data, on_set_sensor);
		break;
	default:
		{
			/* Send command not support reply message*/
			char repMsg[32] = {0};
			int len = 0;
			sprintf( repMsg, "{\"errorRep\":\"Unknown cmd!\"}" );
			len= strlen( "{\"errorRep\":\"Unknown cmd!\"}" ) ;
			if ( g_sendcbf ) g_sendcbf( & g_HandlerInfo, hk_error_rep, repMsg, len, NULL, NULL );
		}
		break;
	}
}

/* **************************************************************************************
 *  Function Name: Handler_AutoReportStart
 *  Description: Start Auto Report
 *  Input : char *pInQuery
 *  Output: None
 *  Return: None
 * ***************************************************************************************/
void HANDLER_API Handler_AutoReportStart(char *pInQuery)
{
	/*TODO: Parsing received command
	*input data format: 
	* {"susiCommData":{"catalogID":4,"autoUploadIntervalSec":30,"requestID":1001,"requestItems":["all"],"commCmd":2053}}
	*
	* "autoUploadIntervalSec":30 means report sensor data every 30 sec.
	* "requestItems":["all"] defined which handler or sensor data to report. 
	*/
	WASCADAHLog(Debug, "> %s Start Report", strHandlerName);
	/*create thread to report sensor data*/
	HandlerKernel_AutoReportStart(pInQuery);
}

/* **************************************************************************************
 *  Function Name: Handler_AutoReportStop
 *  Description: Stop Auto Report
 *  Input : None
 *  Output: None
 *  Return: None
 * ***************************************************************************************/
void HANDLER_API Handler_AutoReportStop(char *pInQuery)
{
	/*TODO: Parsing received command*/
	WASCADAHLog(Debug, "> %s Stop Report", strHandlerName);

	HandlerKernel_AutoReportStop(pInQuery);
}

/* **************************************************************************************
 *  Function Name: Handler_Get_Capability
 *  Description: Get Handler Information specification. 
 *  Input :  None
 *  Output: char ** : pOutReply       // JSON Format
 *  Return:  int  : Length of the status information in JSON format
 *                :  0 : no data need to trans
 * **************************************************************************************/
int HANDLER_API Handler_Get_Capability( char ** pOutReply ) // JSON Format
{
	char* result = NULL;
	int len = 0;

	WASCADAHLog(Debug, "> %s Get Capability", strHandlerName);

	if(!pOutReply) return len;

	/*Create Capability Message Structure to describe sensor data*/
	if(!g_Capability)
	{
		g_Capability = CreateCapability();
		HandlerKernel_SetCapability(g_Capability, false);
	}
	/*generate capability JSON string*/
	result = IoT_PrintCapability(g_Capability);

	/*create buffer to store the string*/
	len = strlen(result);
	*pOutReply = (char *)malloc(len + 1);
	memset(*pOutReply, 0, len + 1);
	strcpy(*pOutReply, result);
	free(result);
	return len;
}

/* **************************************************************************************
 *  Function Name: Handler_MemoryFree
 *  Description: free the memory allocated for Handler_Get_Capability
 *  Input : char *pInData.
 *  Output: None
 *  Return: None
 * ***************************************************************************************/
void HANDLER_API Handler_MemoryFree(char *pInData)
{
	WASCADAHLog(Debug, "> %s Free Allocated Memory", strHandlerName);

	if(pInData)
	{
		free(pInData);
		pInData = NULL;
	}
	return;
}


bool read_INI()
{
	char modulePath[200]={0};
	char iniPath[200]={0};
	char str[100]={0};
	int i=0;

	FILE *fPtr;

	char *temp_INI_name=NULL;
	
	temp_INI_name=(char *)calloc(strlen(strHandlerName)+1+4,sizeof(char));	//+4 for ".ini"
	strcpy(temp_INI_name, strHandlerName);
	strcat(temp_INI_name,".ini");
	// Load ini file
	util_module_path_get(modulePath);
	util_path_combine(iniPath,modulePath,temp_INI_name);

	printf("iniFile: %s\n",iniPath);

	fPtr = fopen(iniPath, "r");
    if (fPtr)
	{
        printf("INI Opened Successfully...\n");
		bFind=true;

		// [Settings]
		strcpy(INI_data.Server, GetIniKeyString("Setting", "Server", iniPath));
		strcpy(INI_data.Authorization, GetIniKeyString("Setting", "Authorization", iniPath));

		INI_data.RetrieveInterval_ms = GetIniKeyInt("Setting","RetrieveInterval_ms", iniPath);
		if(INI_data.RetrieveInterval_ms <= 1000)
		{
			INI_data.RetrieveInterval_ms = 1000;
		}

		INI_data.RetrieveSpecificTags = GetIniKeyInt("Setting","RetrieveSpecificTags", iniPath);
		if(INI_data.RetrieveSpecificTags == 1)
		{
			INI_data.RetrieveSpecificTags = true;
		}
		else
		{
			INI_data.RetrieveSpecificTags = false;
		}

		INI_data.PollingTagInterval_sec = GetIniKeyInt("Setting","PollingTagInterval_sec", iniPath);
		if(INI_data.PollingTagInterval_sec <= 1)
		{
			INI_data.PollingTagInterval_sec = 1;
		}

		INI_data.PollingTagNotification = GetIniKeyInt("Setting","PollingTagNotification", iniPath);
		if(INI_data.PollingTagNotification == 1)
		{
			INI_data.PollingTagNotification = true;
		}
		else
		{
			INI_data.PollingTagNotification = false;
		}


		if(INI_data.RetrieveSpecificTags == true)
		{
			// [Tags]
			INI_data.numberOfTags = GetIniKeyInt("Tags", "numberOfTags", iniPath);
			if(INI_data.numberOfTags > MAX_NAMELIST_NUM)
			{
				INI_data.numberOfTags = MAX_NAMELIST_NUM;
			}

			int numberOfTags = INI_data.numberOfTags;
			int numberOfTags_Regs = numberOfTags;

			if(numberOfTags !=0)
			{	
				INI_data.pTags = (pTAG_context)calloc(numberOfTags, sizeof(TAG_context));
			}

			for(int i=0; i<numberOfTags; i++)
			{
				char strTagLabel[30];
				INI_data.pTags[i].TagNumber = i;
				sprintf(strTagLabel, "Tag%d", i);

				char strTagLine[200];
				strcpy(strTagLine, "");
				strcpy(strTagLine, GetIniKeyString("Tags", strTagLabel, iniPath));

				// Get Project Name
				char* pTempProjectName = strchr(strTagLine, ',');
				if(pTempProjectName != NULL)
				{
					strcpy(INI_data.pTags[i].ProjectName, "");
					strncpy(INI_data.pTags[i].ProjectName, strTagLine, pTempProjectName-strTagLine);
					INI_data.pTags[i].ProjectName[pTempProjectName-strTagLine+1] = 0;
				}
				else
				{
					INI_data.numberOfTags = i;
					break;
				}

				// Get Tag Name
				char* pTempTagName = strchr(strTagLine, ',');
				if(pTempTagName != NULL)
				{
					pTempTagName = pTempTagName + 1;
					strcpy(INI_data.pTags[i].TagName, pTempTagName);
				}
				else
				{
					INI_data.numberOfTags = i;
					break;
				}

				printf("Tags[%d]: TagNumber=%d, ProjectName=%s, TagName=%s\n", i, INI_data.pTags[i].TagNumber, INI_data.pTags[i].ProjectName, INI_data.pTags[i].TagName);
			}
		}
		else
		{
			INI_data.pTags = (pTAG_context)calloc(MAX_NAMELIST_NUM, sizeof(TAG_context));
			InitTags();
		}

		free(temp_INI_name);
		fclose(fPtr);
		return true;
	}
    else
	{
        printf("INI Opened Fail...\n");
		bFind=false;
		free(temp_INI_name);
		return false;
    }
	return true;
}


bool PrepareEvents(char* eventId, char* eventSubtype, char* eventMessage, char* eventDescription, int eventSeverity)
{
	bool bRetValue = TRUE;
	EventNotify_Context eventNotify;

	if(eventId != NULL && eventSubtype != NULL && eventMessage != NULL && eventDescription != NULL && eventSeverity >= 0 && INI_data.PollingTagNotification == true)
	{
		// Event Id
		strcpy(eventNotify.Id, eventId);
		// Event extend message
		strcpy(eventNotify.extMsg, eventMessage);
		// Event handler
		strcpy(eventNotify.handler, strHandlerName);
		// Event description
		strcpy(eventNotify.msg, eventDescription);
		// Event subType
		strcpy(eventNotify.subtype, eventSubtype);
		// Event Severity
		//eventNotify.severity = Severity_Error; //Severity_Warning; //Severity_Alert;
		eventNotify.severity = eventSeverity;
		ReportEventNotify(&eventNotify);
	}
	return bRetValue;
}


bool ReportEventNotify(EventNotify_ContextPtr pNotifyData)
{
	MSG_CLASSIFY_T *eventnotifyGroup = NULL;
	char* reportEvent = NULL;
	bool reState = false;


	if (pNotifyData == NULL)
		return false;

	if (pNotifyData != NULL)
	{
		if (eventnotifyGroup == NULL)
		{
			eventnotifyGroup = MSG_CreateRoot();
			//eventnotifyGroup = MSG_AddClassify(eventnotifyGroup, HANDLE_EVENT_NOTIFY_CLASSIFY, NULL, false, false);
		}

		// subtype
		MSG_ATTRIBUTE_T* subtype = MSG_FindAttribute(eventnotifyGroup, "subtype", false);
		if (subtype == NULL)
		{
			subtype = MSG_AddAttribute(eventnotifyGroup, "subtype", false);
		}
		MSG_SetStringValue(subtype, pNotifyData->subtype, "r");

		// handler
		MSG_ATTRIBUTE_T* handlername = MSG_FindAttribute(eventnotifyGroup, "handler", false);
		if (handlername == NULL)
		{
			handlername = MSG_AddAttribute(eventnotifyGroup, "handler", false);
		}
		MSG_SetStringValue(handlername, pNotifyData->handler, "r");

		// severity
		MSG_ATTRIBUTE_T* severity = MSG_FindAttribute(eventnotifyGroup, "severity", false);
		if (severity == NULL)
		{
			severity = MSG_AddAttribute(eventnotifyGroup, "severity", false);
		}
		MSG_SetDoubleValue(severity, pNotifyData->severity, "r", NULL);

		// msg
		MSG_ATTRIBUTE_T* msg = MSG_FindAttribute(eventnotifyGroup, "msg", false);
		if (msg == NULL)
		{
			msg = MSG_AddAttribute(eventnotifyGroup, "msg", false);
		}
		MSG_SetStringValue(msg, pNotifyData->msg, "r");

		// extMsg group
		MSG_CLASSIFY_T* extMsg = MSG_FindClassify(eventnotifyGroup, "extMsg");
		if (extMsg == NULL)
		{
			extMsg = MSG_AddClassify(eventnotifyGroup, "extMsg", NULL, false, false);
		}

		// extMsg
		MSG_ATTRIBUTE_T* n = MSG_FindAttribute(extMsg, "n", false);
		if (n == NULL)
		{
			n = MSG_AddAttribute(extMsg, "n", false);
		}
		MSG_SetStringValue(n, pNotifyData->extMsg, NULL);
		//char extMsgString[256] = { "Extend message"};
		//MSG_SetStringValue(n, extMsgString, NULL);

		// eventID
		MSG_ATTRIBUTE_T* eventID = IoT_FindGroupAttribute(extMsg, "eventID");
		if (eventID == NULL)
		{
			eventID = IoT_AddGroupAttribute(extMsg, "eventID");
		}
		IoT_SetStringValue(eventID, pNotifyData->Id, IoT_READONLY);

		reportEvent = MSG_PrintUnformatted(eventnotifyGroup);

		if (g_sendeventcbf)
		{
			g_sendeventcbf(&g_HandlerInfo, (HANDLER_NOTIFY_SEVERITY)pNotifyData->severity, reportEvent, strlen(reportEvent)+1, NULL, NULL);
		}

		if (reState != true)
		{
			reState = true;
		}

		MSG_ReleaseRoot(eventnotifyGroup);
		eventnotifyGroup = NULL;
	}
	return reState;
}