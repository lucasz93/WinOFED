
#include "stdio.h"
#include "string.h"
#include "stdlib.h"
#include <windows.h>
#include <iba/ib_types.h>
#include <iba/ib_al.h>
#include "al_dev.h"

#define IS_FULL_MEMBER_PKEY(pkey)	(0x8000 & (pkey))

BOOLEAN verbose = FALSE;

typedef enum
{
	pkey_show = 0,
	pkey_add,
	pkey_rem
}Pkey_action;

/* common query parameters */
typedef struct _REQUEST_IN
{
	union
	{
		struct
		{
			net64_t			   port_guid;
			unsigned short     pkey_num;
			unsigned __int16   pkeys[MAX_NUM_PKEY];
			char			   name[MAX_NUM_PKEY][MAX_USER_NAME_SIZE];
			Pkey_action		   action;
		}guid_pkey;
		}u;
}REQUEST_IN;

#define	DEFAULT_BUFER_SIZE	2048
static const char IBBUS_SERV_KEY[] = {"SYSTEM\\CurrentControlSet\\Services\\ibbus\\Parameters"};

void show_help()
{
	printf("\nUsage: \n\tpart_man.exe [-v] <show|add|rem> <port_guid> <pkey1-name1 pkey2-name2 ...>\n");
	printf("\nParameters: \n\tport_guid - port guid as in VSTAT, e.g. 0002:C903:002E:8681 \n");
	printf("\tpkey - any 4-digit hex number with MSB bit set, e.g. 803f\n");
	printf("\tname - any printable name without ':', ',', ';' '-' and ' ' and starting from i or e\n");
	printf("\nExample: \n\tpart_man add 0002:C903:002E:8681 0x8034-ipoib_27 8034-ipoib_28 8033-iwert#9 \n");
}

/********************************************************************
* name	:	reg_ibbus_pkey_show
*			read registry pkey and optionally prints it
* input	:	show - whether to print
* output:	partKey - contains read pkeys, reg_handle
* return:   number of characters read
********************************************************************/
static int reg_ibbus_pkey_show(IN BOOLEAN show,OUT char *partKey, OUT HKEY *reg_handle)
{
	LONG   ret;
	int retval;
	DWORD  read_length = DEFAULT_BUFER_SIZE;
	const char *sep_group = ";";
	const char *sep_guid_pkey = ":,";

	ret = RegOpenKeyEx(HKEY_LOCAL_MACHINE,IBBUS_SERV_KEY,0,KEY_SET_VALUE | KEY_QUERY_VALUE ,reg_handle);
	if (ERROR_SUCCESS != ret)
	{
		return 0;
	}	

	do
	{
		ret = RegQueryValueEx(*reg_handle,"PartitionKey",NULL,NULL,(LPBYTE)partKey,&read_length);
		if (ERROR_MORE_DATA == ret)
		{
			retval = 0;
			break;
		}

		if (ERROR_SUCCESS != ret)
		{
			retval = 0;
			break;
		}
		retval = (int)read_length;
		if(retval > 4)
		{
			if(show)
			{
				char *pkey_group, *pkey_group_next, *pkey_guid, tmp;
				int i,j,k;
				unsigned short *guid_vstat;
				char* next_token = NULL;

				if (verbose) {
					printf("PartitionKey, read from Registry (size %d): \n  %s\n", read_length, partKey);
				}
				
				pkey_group = partKey;
				while(pkey_group && (*pkey_group != '\0'))
				{
					pkey_group_next = strstr(pkey_group,sep_group);
					if(pkey_group_next)
						*pkey_group_next = '\0';
					pkey_guid = strtok_s(pkey_group,sep_guid_pkey, &next_token);
					i = 0;
					while(pkey_guid)
					{
						if(i == 0)
						{
							j = 1;
							for (k = strlen(pkey_guid) -1; k > 1; k--)
							{
								if (j%2)
								{
									tmp = pkey_guid[k -1];
									pkey_guid[k -1] = pkey_guid[k];
									pkey_guid[k] = tmp;
								}
								printf("%c",pkey_guid[k]);
								if ((j%4 == 0) && (k > 2))
								{
									printf(":");
									j = 1;
								}
								else
									j++;
							}
							printf("\t");
						}
						else
						{
							printf("%s\t",pkey_guid);
						}
						pkey_guid = strtok_s(NULL,sep_guid_pkey, &next_token);
						if((++i)%5 == 0)
							printf("\n\t\t\t");
					}
					printf("\n\n");
					if(pkey_group_next)
						pkey_group = pkey_group_next + 1;
					else
						pkey_group = NULL;
				}
			}
		}
		else
			retval = 0;
	}
	while(FALSE);
	return retval;
}

static int reg_ibbus_print_pkey()
{
	int result;
	char pkeyBuf[DEFAULT_BUFER_SIZE];
	HKEY hreg = NULL;

	result = reg_ibbus_pkey_show(TRUE,(char*)pkeyBuf,&hreg);
	if(hreg)
		RegCloseKey( hreg );

	if (result < 4)
	{
		printf("No configured pkey found\n");
		return 1;
	}
	return 0;
}

static int reg_ibbus_pkey_add(const uint16_t *pkeys, char *names, uint16_t pkey_num,OUT pkey_array_t *pkey, OUT char **final_reg_string, OUT DWORD *reg_length)
{
	static char partKey[DEFAULT_BUFER_SIZE];
	char tmp[20];
	char *guid_string, *p;
	HKEY reg_handle;
	LONG   ret;
	char *tmpbuff = NULL;
	int cnt;
	int retval = 0;
	uint16_t i = 0;
	DWORD  read_length;
	char *p_name = names;

	*final_reg_string = NULL;
	read_length = reg_ibbus_pkey_show(FALSE,(char*)partKey,&reg_handle);
	p = NULL;
	guid_string = NULL;
	if (read_length < 4)
	{
		/* empty string read, simply write to registry */
		cnt = sprintf_s(partKey, DEFAULT_BUFER_SIZE, "0x%I64X:",pkey->port_guid);
	}
	else
	{
		/* update the existed registry list */
		sprintf_s(tmp, 20, "0x%I64X",pkey->port_guid);
		guid_string = strstr(partKey,tmp);
		if(guid_string)
		{
			p = strstr(guid_string,";");
			tmpbuff = (char*)malloc(strlen(p) + 1);
			if(!tmpbuff)
			{
				printf("Failed memory allocation\n");
				return 1;
			}
			/* save the rest of the string */
			strcpy_s(tmpbuff, strlen(p) + 1, p);
			cnt = (int)(p - partKey);
		}
		else
		{
			sprintf_s(partKey + strlen(partKey), DEFAULT_BUFER_SIZE - strlen(partKey), "%s:",tmp);
			cnt = strlen(partKey);
		}
	}	

	for (i = 0 ;i < pkey_num; i++, p_name+=MAX_USER_NAME_SIZE)
	{
		char *same_pkey;
		sprintf_s(tmp, 20, "0x%04X-%s",pkeys[i],p_name);
		if ( guid_string )
		{
			same_pkey = strstr(guid_string,tmp);
			if( same_pkey && (same_pkey < p) )
				continue;
		}
		pkey->pkey_array[pkey->pkey_num] = pkeys[i];
		strcpy_s( pkey->name[pkey->pkey_num], MAX_USER_NAME_SIZE, p_name );
		pkey->pkey_num++;
		if( (i == 0) && (!guid_string))
			cnt += sprintf_s(partKey + cnt, DEFAULT_BUFER_SIZE - cnt, "0x%04X-%s",pkeys[i],p_name);
		else
			cnt += sprintf_s(partKey + cnt, DEFAULT_BUFER_SIZE - cnt, ",0x%04X-%s",pkeys[i],p_name);
	}
	if(tmpbuff)
	{
		cnt += sprintf_s(partKey + cnt, DEFAULT_BUFER_SIZE - cnt, "%s",tmpbuff);
		free(tmpbuff);
	}
	else
		cnt += sprintf_s(partKey + cnt, DEFAULT_BUFER_SIZE - cnt, ";\0");

	if(pkey->pkey_num)
	{			
		*final_reg_string = partKey;
		*reg_length = (DWORD)cnt;
		if (verbose) {
			printf("PartitionKey, prepared for storing in Registry (size %d): \n  %s\n", cnt, partKey);
		}
	}
	else
	{
		printf("Required pkeys already exist\n");
		retval = 1;
	}
	RegCloseKey( reg_handle );
	return retval;
}

static int reg_ibbus_pkey_rem(const unsigned __int16 *pkeys, char *names, unsigned short pkey_num,OUT pkey_array_t *pkey)
{
	static char partKey[DEFAULT_BUFER_SIZE];
	static char newKey[DEFAULT_BUFER_SIZE] = {'\0'};

	HKEY reg_handle;
	LONG   ret;
	DWORD  read_length;
	int converted,cnt;
	unsigned __int16 cur_pkey;
	char cur_name[MAX_USER_NAME_SIZE];
	int retval = 0;
	unsigned short i = 0;
	char pkey_sep[] = ",";
	char *pfrom, *pto;
	char *guid_string;
	char tmp[20];
	char *token;
	char* next_token = NULL;
	char *pafter = NULL;
	boolean_t found2remove;
	boolean_t pkey_not_written = TRUE;
	char *p_name;

	read_length = reg_ibbus_pkey_show(FALSE,(char*)partKey,&reg_handle);
	do
	{
		if (read_length < 4)
		{
			/* empty string read, nothing to delete */
			printf("No pkey configured - nothing to remove\n");
			retval = 1;
			break;
		}

		sprintf_s(tmp, 20, "0x%I64X\0",pkey->port_guid);
		guid_string = strstr(partKey,tmp);
		if (! guid_string)
		{
			printf("No guid configured - nothing to remove\n");
			retval = 1;
			break;
		}
		pfrom = strstr(guid_string,":");
		pto   = strstr(guid_string,";");
		if ( (!pfrom) || (!pto))
		{
			printf("Error configuration\n");
			retval = 1;
			break;
		}

		pfrom++;
		pafter  = (char*)malloc(strlen(pto) + 1);

		if(!pafter)
		{
			printf("Allocation failed\n");
			retval = 1;
			break;
		}
		_snprintf_s(newKey, DEFAULT_BUFER_SIZE, (int)(pfrom - partKey),"%s",partKey);
		cnt = (int)(pfrom - partKey);
		strcpy_s(pafter, strlen(pto) + 1, pto);
		pto[0] = '\0';
		strcpy_s(partKey, DEFAULT_BUFER_SIZE, pfrom);
		token = strtok_s(partKey, pkey_sep, &next_token);
		while(token)
		{
			found2remove = FALSE;
			cur_pkey = 0;
			cur_name[0] = '\0';
			converted = sscanf_s(token,"0x%04X-%s",&cur_pkey,cur_name);
			if(!converted || (converted == EOF))
			{
				printf("invalid registry format\n");
				retval = 1;
				break;
			}

			for (i = 0, p_name = names; i < pkey_num; i++, p_name+=MAX_USER_NAME_SIZE)
			{
				found2remove = (boolean_t)((cur_pkey == pkeys[i]) && !strcmp(cur_name,p_name));
				if(found2remove)
				{
					pkey->pkey_array[pkey->pkey_num] = pkeys[i];
					strcpy_s(pkey->name[pkey->pkey_num], MAX_USER_NAME_SIZE, p_name);
					break;
				}
			}
			
			if(found2remove)
			{
				pkey->pkey_num++;
			}
			else
			{
				if(pkey_not_written)
				{
					cnt += sprintf_s(newKey + cnt, DEFAULT_BUFER_SIZE - cnt, "0x%04X-%s",cur_pkey,cur_name);
					pkey_not_written = FALSE;
				}
				else
					cnt += sprintf_s(newKey + cnt, DEFAULT_BUFER_SIZE - cnt, ",0x%04X-%s",cur_pkey,cur_name);
			}
			token = strtok_s(NULL, pkey_sep, &next_token);
		}

		if(! pkey->pkey_num)
		{
			/* nothing to delete */
			printf("Nothing to remove\n");
			retval = 1;
			break;
		}

		if(pkey_not_written)
			cnt -= (2 + strlen(tmp));
		
		strcpy_s(newKey + cnt, DEFAULT_BUFER_SIZE - cnt, pafter);
		if (verbose) {
			printf("PartitionKey, prepared for storing in Registry (size %d): \n  %s\n", strlen(newKey), newKey);
		}
		ret = RegSetValueEx(reg_handle,"PartitionKey",0,REG_SZ,(BYTE*)newKey, (DWORD)strlen(newKey));
		if (ERROR_SUCCESS != ret)
		{
			printf("registry operation failed error = %d\n",GetLastError());
			retval = 1;
			break;
		}
	}
	while(FALSE);
	if(pafter)
		free(pafter);

	RegCloseKey( reg_handle );
	return retval;
}

static int send_pdo_req(pkey_array_t *pkeys,DWORD iocode)
{
	HANDLE hKernelLib;
	DWORD		bytesReturned;

	hKernelLib =
		CreateFile(
		"\\\\.\\ibal",
		GENERIC_READ | GENERIC_WRITE,
		FILE_SHARE_READ | FILE_SHARE_WRITE, // share mode none
		NULL,                               // no security
		OPEN_EXISTING,
		FILE_ATTRIBUTE_NORMAL,
		NULL                                // no template
		);

	if (hKernelLib == INVALID_HANDLE_VALUE)
	{
		printf("failed to open the kernel device : error = %d\n",GetLastError());
		return 1;
	}

	if (! DeviceIoControl(hKernelLib,
						  iocode,
						  pkeys,sizeof(pkey_array_t),
						  NULL,0,
						  &bytesReturned,
						  NULL))
	{
		DWORD err = GetLastError();
		if (err == 1168)
			printf("No matched port guid (0x%I64X) found\n",pkeys->port_guid);
		else if (err == 1117)
			printf("operation failed - internal driver error\n");
		else if(err == 87)
			printf("operation failed - invalid input to driver\n");
		else
			printf("operation failed with error %d\n",err);

		CloseHandle(hKernelLib);
		return 1;
	}
	CloseHandle(hKernelLib);
	return 0;
}


boolean_t reg_pkey_operation(const REQUEST_IN *input)
{
	pkey_array_t pkeys;
	HKEY reg_handle;
	char *p_reg_string = NULL;
	DWORD reg_length = 0;
	int result;
	int i;
	LONG   ret;
	if(!input)
	{
		printf("invalid input parameter\n");
		return FALSE;
	}

	RtlZeroMemory(&pkeys,sizeof(pkeys));
	pkeys.port_guid = input->u.guid_pkey.port_guid;

	if(input->u.guid_pkey.action == pkey_add)
		result = reg_ibbus_pkey_add((unsigned __int16*)input->u.guid_pkey.pkeys, (char*)input->u.guid_pkey.name, input->u.guid_pkey.pkey_num, &pkeys,&p_reg_string,&reg_length);
	else if(input->u.guid_pkey.action == pkey_rem)
		result = reg_ibbus_pkey_rem((unsigned __int16*)input->u.guid_pkey.pkeys, (char*)input->u.guid_pkey.name, input->u.guid_pkey.pkey_num, &pkeys);
	else if(input->u.guid_pkey.action == pkey_show)
	{	
		reg_ibbus_print_pkey();
		return TRUE;
	}
	else
		printf("Invalid command to part_man.exe\n");

	if( 0 != result)
		return FALSE;

	if(pkeys.pkey_num)
	{
		if(input->u.guid_pkey.action == pkey_add)
		{
			if( 0 == send_pdo_req(&pkeys,UAL_REQ_CREATE_PDO))
			{
				ret = RegOpenKeyEx(HKEY_LOCAL_MACHINE,IBBUS_SERV_KEY,0,KEY_SET_VALUE | KEY_QUERY_VALUE ,&reg_handle);
				ret = RegSetValueEx(reg_handle,"PartitionKey",0,REG_SZ,(BYTE*)p_reg_string,reg_length);
				RegCloseKey( reg_handle );
				if (ERROR_SUCCESS == ret)
				{
					return TRUE;
				}
				else
				{
					printf("registry operation failed = %d\n",GetLastError());
				}
			}
		}
		else if(input->u.guid_pkey.action == pkey_rem)
		{
			return (boolean_t)( 0 == send_pdo_req(&pkeys,UAL_REQ_REMOVE_PDO));
		}
	}
	return FALSE;
}

int prepare_reg_pkey_input(OUT REQUEST_IN *input,char* cmd[],int num)
{
	int i;
	unsigned short pkey_num;
	char name[256];

	input->u.guid_pkey.pkey_num = 0;
	input->u.guid_pkey.port_guid = 0;

	if(strstr(cmd[1],"add"))
		input->u.guid_pkey.action = pkey_add;
	else if(strstr(cmd[1],"rem"))
		input->u.guid_pkey.action = pkey_rem;
	else if(strstr(cmd[1],"show"))
	{
		input->u.guid_pkey.action = pkey_show;
		return 1;
	}
	else
		goto bad_cmd;

	if(num < 4)
		goto bad_num;

    /* vstat output format 0008:f104:0397:7ccc 
	   For port guid add 1 for each port 
	 */
	if (strstr(cmd[2],":"))
	{
		int i;
		unsigned short *guid_vstat;
		guid_vstat = (unsigned short*)&input->u.guid_pkey.port_guid;
		if (4 == sscanf_s(cmd[2],"%x:%x:%x:%x",&guid_vstat[0],&guid_vstat[1],&guid_vstat[2],&guid_vstat[3]))
		{
			for( i = 0; i < 4; i++)
				guid_vstat[i] = (guid_vstat[i] << 8) | (guid_vstat[i] >> 8);
		}
		else
		{
			goto bad_guid;
		}
	}
	else
	{
		goto bad_guid;
	}

	for( i = 3, pkey_num = 0; i < num; i++)
	{
		name[0] = '\0';
		if (strstr(cmd[i],"0x") || strstr(cmd[i],"0X"))
			sscanf_s(cmd[i],"0x%x-%s",&input->u.guid_pkey.pkeys[pkey_num], name);
		else
			sscanf_s(cmd[i],"%x-%s",&input->u.guid_pkey.pkeys[pkey_num], name);

		//  name sanity checks
		if ( strlen(name) == 0 )
			goto bad_name1;
		if ( strlen(name) >= MAX_USER_NAME_SIZE )
			goto bad_name2;
		if ( strstr(name,"-") )
			goto bad_name3;
		if ( strstr(name,":") )
			goto bad_name3;
		if ( strstr(name,";") )
			goto bad_name3;
		if ( strstr(name,",") )
			goto bad_name3;
		if ( !(name[0] == 'i' || name[0] == 'I' || name[0] == 'e' || name[0] == 'E') )
			goto bad_name4;
		if ( name[0] == 'e' || name[0] == 'E' )
			goto bad_name5;

		strcpy_s(input->u.guid_pkey.name[pkey_num], MAX_USER_NAME_SIZE, name);

		if (! IS_FULL_MEMBER_PKEY(input->u.guid_pkey.pkeys[pkey_num]))
	    {
			printf("partial member pkey %s is not suported\n",cmd[i]);
			return 0;
		}
		pkey_num++;
	}
	input->u.guid_pkey.pkey_num = pkey_num;
	return 1;

bad_cmd:
	printf("ERROR: Invalid command %s\n",cmd[1]);
	goto exit;

bad_num:
	printf("ERROR: Insufficient number of parameters %s\n",cmd[1]);
	goto exit;

bad_guid:
	printf("ERROR: port guid %s - illegal port guid format, expected xxxx:xxxx:xxxx:xxxx\n",cmd[2]);
	goto exit;

bad_name1:	
	printf("ERROR: The name is absent.\n", name, MAX_USER_NAME_SIZE-1 );
	goto exit;

bad_name2:	
	printf("ERROR: The name '%s' is too long (%d). Max size is %d.\n", name, strlen(name), MAX_USER_NAME_SIZE-1 );
	goto exit;

bad_name3:	
	printf("ERROR: Illegal name '%s'. The name shouldn't contain the following characters: '-', ';', ':', ','\n", name);
	goto exit;

bad_name4:	
	printf("ERROR: Illegal name '%s'. The name should start from 'i' for IPoIB or from for EoIB\n", name);
	goto exit;

bad_name5:	
	printf("ERROR: Illegal name '%s'. EoIB is not supported now\n", name);
	goto exit;

exit:
	show_help();
	return 0;
}

void partition_operation(char* cmd[],int num)
{
	REQUEST_IN input;

	if (! prepare_reg_pkey_input(&input, cmd, num))
		return;
	if (verbose) {
		int i;
		char *action[] = { "show", "add", "rem" };
		unsigned short *guid = (unsigned short *)&input.u.guid_pkey.port_guid;

		printf( "Input: action '%s', guid %04hx:%04hx:%04hx:%04hx\n", action[input.u.guid_pkey.action], 
			cl_ntoh16(guid[0]), cl_ntoh16(guid[1]), cl_ntoh16(guid[2]), cl_ntoh16(guid[3]));
		for ( i = 0; i < input.u.guid_pkey.pkey_num; ++i ) {
			printf( "  pkey 0x%hx, name '%s'\n", input.u.guid_pkey.pkeys[i], input.u.guid_pkey.name[i] );
		}
	}

	if(! reg_pkey_operation(&input))
		printf("Pkey operation failed\n");	
	else
		printf("Done...\n");
}

int32_t __cdecl
main(
	int32_t argc,
	char* argv[])
{
	BOOLEAN showHelp = FALSE;
	int shift = 0;
	
	if (argc < 2)
	{
		showHelp = TRUE;
	}
	else
	{
		if(!_stricmp(argv[1], "-h") || !_stricmp(argv[1], "-help"))
		{
			showHelp = TRUE;
		}
		else {
			if(!_stricmp(argv[1], "-v")) 
			{
				verbose = TRUE;
				shift = 1;
			}
			partition_operation(&argv[shift],argc-shift);
		}
	}
	if (showHelp)
		show_help();
}


