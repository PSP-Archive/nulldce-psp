#include "common.h"
#include "cdi.h"
#include "mds.h"
#include "iso9660.h"
#include "ioctl.h"
#include "os_base.h"

#include "pspDmac.h"

u32 NullDriveDiscType;
DriveIF* CurrDrive;
DriveIF drives[]=
{
	{
		//cdi
		cdi_DriveReadSector,
			cdi_DriveGetTocInfo,
			cdi_DriveGetDiscType,
			cdi_GetSessionsInfo,
			cdi_init,
			cdi_term,
			"CDI reader"
	},
	{
		//cdi
		mds_DriveReadSector,
		mds_DriveGetTocInfo,
		mds_DriveGetDiscType,
		mds_GetSessionsInfo,
		mds_init,
		mds_term,
		"NRG/MDS/MDF reader"
	},
	{
		//iso
		iso_DriveReadSector,
		iso_DriveGetTocInfo,
		iso_DriveGetDiscType,
		iso_GetSessionsInfo,
		iso_init,//these need to be filled
		iso_term,
		"ISO reader"
	},
};

u8 q_subchannel[96];

u32 settings_PatchRegion=1;
void PatchRegion_0(u8* sector,int size)
{
	if (settings_PatchRegion==0)
		return;

	u8* usersect=sector;

	if (size!=2048)
	{
		printf("PatchRegion_0 -> sector size %d , skiping patch\n",size);
	}

	//patch meta info
	u8* p_area_symbol=&usersect[0x30];
	memcpy(p_area_symbol,"JUE        ",8);
}
void PatchRegion_6(u8* sector,int size)
{
	if (settings_PatchRegion==0)
		return;

	u8* usersect=sector;

	if (size!=2048)
	{
		printf("PatchRegion_6 -> sector size %d , skiping patch\n",size);
	}

	//patch area symbols
	u8* p_area_text=&usersect[0x700];
	memcpy(&p_area_text[4],"For JAPAN,TAIWAN,PHILIPINES.",28);
	memcpy(&p_area_text[4 + 32],"For USA and CANADA.         ",28);
	memcpy(&p_area_text[4 + 32 + 32],"For EUROPE.                 ",28);
}
bool ConvertSector(u8* in_buff , u8* out_buff , int from , int to,int sector)
{
	sceKernelDcacheWritebackInvalidateAll();

	//if no convertion
	if (to==from)
	{
		sceDmacMemcpy(out_buff,in_buff,to);
		return true;
	}
	//get subchannel data, if any
	if (from==2448)
	{
		sceDmacMemcpy(q_subchannel,in_buff+2352,96);
		from-=96;
	}
	switch (to)
	{
	case 2048:
		{
			verify(from>=2048);
			verify((from==2448) || (from==2352) || (from==2336));
			if ((from == 2352) || (from == 2448))
			{
				if (in_buff[15]==1)
				{
					sceDmacMemcpy(out_buff,&in_buff[0x10],2048); //0x10 -> mode1
				}
				else
					sceDmacMemcpy(out_buff,&in_buff[0x18],2048); //0x18 -> mode2 (all forms ?)
			}
			else
				sceDmacMemcpy(out_buff,&in_buff[0x8],2048);	//hmm only possible on mode2.Skip the mode2 header
		}
		break;
	case 2352:
		//if (from >= 2352)
		{
			sceDmacMemcpy(out_buff,&in_buff[0],2352);
		}
		break;
	default :
		printf("Sector convertion from %d to %d not supported \n", from , to);
		break;
	}

	sceKernelDcacheWritebackInvalidateAll();

	return true;
}

bool InitDrive_(wchar* fn)
{
	if (CurrDrive !=0 && CurrDrive->Inited==true)
	{
		//Terminate
		CurrDrive->Inited=false;
		CurrDrive->Term();
	}

	CurrDrive=0;

	for (u32 i=0;i<3;i++)
	{
		if (drives[i].Init(fn))
		{
			NullDriveDiscType=Busy;
			NotifyEvent_gdrom(DiskChange,0);
//			Sleep(400); //busy for a bit

			CurrDrive=&drives[i];
			CurrDrive->Inited=true;
			printf("Using %s \n",CurrDrive->name);
			return true;
		}
	}
	//CurrDrive=&drives[Iso];
	NullDriveDiscType=NoDisk; //no disc :)
	return false;
}

#include "cl/cl.h"

bool InitDrive(u32 fileflags)
{
	wchar fn[512];
	
	if (filename)
	{
		strcpy(fn,filename);
	}
	else
	{
		int gfrv=os_GetFile(fn,0,fileflags);
		if (gfrv==0)
		{
			CurrDrive=0;
			NullDriveDiscType=NoDisk;
			return true;
		}
		else if (gfrv==-1)
			return false;
	}

	if (!InitDrive_(fn))
	{
		msgboxf("Selected image failed to load",0);
		CurrDrive=0;
		NullDriveDiscType=NoDisk;
		return true;
	}
	else
		return true;
}

void TermDrive()
{
	if (CurrDrive !=0 && CurrDrive->Inited==true)
	{
		//Terminate
		CurrDrive->Inited=false;
		CurrDrive->Term();
		CurrDrive=0;
	}
}


//
//convert our nice toc struct to dc's native one :)

u32 CreateTrackInfo(u32 ctrl,u32 addr,u32 fad)
{
	u8 p[4];
	p[0]=(ctrl<<4)|(addr<<0);
	p[1]=fad>>16;
	p[2]=fad>>8;
	p[3]=fad>>0;

	return *(u32*)p;
}
u32 CreateTrackInfo_se(u32 ctrl,u32 addr,u32 tracknum)
{
	u8 p[4];
	p[0]=(ctrl<<4)|(addr<<0);
	p[1]=tracknum;
	p[2]=0;
	p[3]=0;
	return *(u32*)p;
}

void ConvToc(u32* to,TocInfo* from)
{
	to[99]=CreateTrackInfo_se(from->tracks[from->FistTrack-1].Control,from->tracks[from->FistTrack-1].Addr,from->FistTrack);
	to[100]=CreateTrackInfo_se(from->tracks[from->LastTrack-1].Control,from->tracks[from->LastTrack-1].Addr,from->LastTrack);
	to[101]=CreateTrackInfo(from->LeadOut.Control,from->LeadOut.Addr,from->LeadOut.FAD);
	for (int i=0;i<99;i++)
	{
		to[i]=CreateTrackInfo(from->tracks[i].Control,from->tracks[i].Addr,from->tracks[i].FAD);
	}
}

void GetDriveToc(u32* to,DiskArea area)
{
	TocInfo driveTOC;
	CurrDrive->GetToc(&driveTOC,area);
	ConvToc(to,&driveTOC);
}

void GetDriveSessionInfo(u8* to,u8 session)
{
	SessionInfo driveSeS;
	if (CurrDrive && CurrDrive->GetSessionInfo)
		CurrDrive->GetSessionInfo(&driveSeS);

	to[0]=2;//status , will get overwrited anyway
	to[1]=0;//0's

	if (session==0)
	{
		to[2]=driveSeS.SessionCount;//count of sessions
		to[3]=driveSeS.SessionsEndFAD>>16;//fad is sessions end
		to[4]=driveSeS.SessionsEndFAD>>8;
		to[5]=driveSeS.SessionsEndFAD>>0;
	}
	else
	{
		to[2]=driveSeS.SessionStart[session-1];//start track of this session
		to[3]=driveSeS.SessionFAD[session-1]>>16;//fad is session start
		to[4]=driveSeS.SessionFAD[session-1]>>8;
		to[5]=driveSeS.SessionFAD[session-1]>>0;
	}
}

void printtoc(TocInfo* toc,SessionInfo* ses)
{
	printf("Sessions %d\n",ses->SessionCount);
	for (u32 i=0;i<ses->SessionCount;i++)
	{
		printf("Session %d: FAD %d,First Track %d\n",i+1,ses->SessionFAD[i],ses->SessionStart[i]);
		for (u32 t=toc->FistTrack-1;t<=toc->LastTrack;t++)
		{
			if (toc->tracks[t].Session==i+1)
			{
				printf("\tTrack %d : FAD %d CTRL %d ADR %d\n",t+1,toc->tracks[t].FAD,toc->tracks[t].Control,toc->tracks[t].Addr);
			}
		}
	}
	printf("Session END: FAD END %d\n",ses->SessionsEndFAD);
}