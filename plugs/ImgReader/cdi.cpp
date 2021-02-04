#include "cdi.h"

#define CDI_V2  0x80000004
#define CDI_V3  0x80000005
#define CDI_V35 0x80000006

unsigned long temp_value;

typedef struct image_s
       {
       long               header_offset;
       long               header_position;
       long               length;
       unsigned long      version;
       unsigned short int sessions;
       unsigned short int tracks;
       unsigned short int remaining_sessions;
       unsigned short int remaining_tracks;
       unsigned short int global_current_session;
} image_s;

typedef struct track_s
       {
       unsigned short int global_current_track;
       unsigned short int number;
       long               position;
       unsigned long      mode;
       unsigned long      sector_size;
       unsigned long      sector_size_value;
       long               length;
       long               pregap_length;
       long               total_length;
       unsigned long      start_lba;
       unsigned char      filename_length;
} track_s;

struct CORE_FILE {
	FILE* f;
	//char * path;
	size_t seek_ptr;

	//char * host;
	int port;
};

typedef void* core_file;


SessionInfo cdi_ses;
TocInfo cdi_toc;
DiscType cdi_Disctype;
struct file_TrackInfo
{
	u32 FAD;
	u32 Offset;
	u32 SectorSize;
};

file_TrackInfo Track[101];

u32 TrackCount;

u8 SecTemp[2352];
FILE* fp_cdi;

void cdi_ReadSSect(u8* p_out,u32 sector,u32 secsz)
{
	printf("CDI READ\n");
	for (u32 i=0;i<TrackCount;i++)
	{
		if (Track[i+1].FAD>sector)
		{
			u32 fad_off=sector-Track[i].FAD;
			fseek(fp_cdi,Track[i].Offset+fad_off*Track[i].SectorSize,SEEK_SET);
			fread(SecTemp,Track[i].SectorSize,1,fp_cdi);

			ConvertSector(SecTemp,p_out,Track[i].SectorSize,secsz,sector);
			break;
		}
	}
	printf("CDI FINISH READ\n");
}
void cdi_DriveReadSector(u8 * buff,u32 StartSector,u32 SectorCount,u32 secsz)
{
	printf("GDR->Read : Sector %d , size %d , mode %d \n",StartSector,SectorCount,secsz);
	while(SectorCount--)
	{
		cdi_ReadSSect(buff,StartSector,secsz);
		buff+=secsz;
		StartSector++;
	}
}

size_t core_ftell(core_file* fc)
{
	CORE_FILE* f = (CORE_FILE*)fc;
	return f->seek_ptr;
}

size_t core_fsize(void* fc)
{
   CORE_FILE* f = (CORE_FILE*)fc;

   if (f->f) {
      size_t p=ftell(f->f);
      fseek(f->f,0,SEEK_END);
      size_t rv=ftell(f->f);
      fseek(f->f,p,SEEK_SET);
      return rv;
   }
   return 0;
}

size_t core_fseek(void* fc, size_t offs, size_t origin) {
	CORE_FILE* f = (CORE_FILE*)fc;
	
	if (origin == SEEK_SET)
		f->seek_ptr = offs;
	else if (origin == SEEK_CUR)
		f->seek_ptr += offs;


	if (f->f)
		fseek(f->f, f->seek_ptr, SEEK_SET);

	return 0;
}

int core_fread(void* fc, void* buff, size_t len)
{
	CORE_FILE* f = (CORE_FILE*)fc;

	if (f->f)
		fread(buff,1,len,f->f);

	f->seek_ptr += len;

	return len;
}

core_file* core_fopen(const char* filename)
{

	CORE_FILE* rv = new CORE_FILE();
	rv->f = 0;
	//rv->path = p;
  {
		rv->f = fopen(filename, "rb");

		if (!rv->f) {
			delete rv;
			return 0;
		}
	}

	core_fseek((core_file*)rv, 0, SEEK_SET);
	return (core_file*)rv;
}


#define FILE core_file
#define fread(buff,sz,cnt,fc) core_fread(fc,buff,sz*cnt)
#define fseek core_fseek
#define ftell core_ftell

void CDI_skip_next_session (FILE *fsource, image_s *image)
{
   fseek(fsource, 4, SEEK_CUR);
   fseek(fsource, 8, SEEK_CUR);
   if (image->version != CDI_V2)
      fseek(fsource, 1, SEEK_CUR);
}

void CDI_get_tracks (FILE *fsource, image_s *image)
{
   fread(&image->tracks, 2, 1, fsource);
}

void CDI_get_sessions (FILE *fsource, image_s *image)
{
#ifndef DEBUG_CDI
   if (image->version == CDI_V35)
      fseek(fsource, (image->length - image->header_offset), SEEK_SET);
   else
      fseek(fsource, image->header_offset, SEEK_SET);

#else
   fseek(fsource, 0L, SEEK_SET);
#endif
   fread(&image->sessions, 2, 1, fsource);
}


bool CDI_init (FILE *fsource, image_s *image, const char *fsourcename)
{
   image->length = core_fsize(fsource);

   if (image->length < 8)
   {
	  printf("%s: Image file is too short\n", fsourcename);
	  return false;
   }

   fseek(fsource, image->length-8, SEEK_SET);
   fread(&image->version, 4, 1, fsource);
   fread(&image->header_offset, 4, 1, fsource);

   if ((image->version != CDI_V2 && image->version != CDI_V3 && image->version != CDI_V35)
		 || image->header_offset == 0)
   {
	  printf("%s: Bad image format\n", fsourcename);
	  return false;
   }
   return true;
}

void CDI_read_track (FILE *fsource, image_s *image, track_s *track)
{
   unsigned char TRACK_START_MARK[10] = { 0, 0, 0x01, 0, 0, 0, 0xFF, 0xFF, 0xFF, 0xFF };
   unsigned char current_start_mark[10];

   fread(&temp_value, 4, 1, fsource);
   if (temp_value != 0)
      fseek(fsource, 8, SEEK_CUR); // extra data (DJ 3.00.780 and up)

   fread(&current_start_mark, 10, 1, fsource);
   if (memcmp(TRACK_START_MARK, current_start_mark, 10)) printf( "Unsupported format: Could not find the track start mark");

   fread(&current_start_mark, 10, 1, fsource);
   if (memcmp(TRACK_START_MARK, current_start_mark, 10)) printf(  "Unsupported format: Could not find the track start mark");

   fseek(fsource, 4, SEEK_CUR);
   fread(&track->filename_length, 1, 1, fsource);
   fseek(fsource, track->filename_length, SEEK_CUR);
   fseek(fsource, 11, SEEK_CUR);
   fseek(fsource, 4, SEEK_CUR);
   fseek(fsource, 4, SEEK_CUR);
   fread(&temp_value, 4, 1, fsource);
   if (temp_value == 0x80000000)
      fseek(fsource, 8, SEEK_CUR); // DJ4
   fseek(fsource, 2, SEEK_CUR);
   fread(&track->pregap_length, 4, 1, fsource);
   fread(&track->length, 4, 1, fsource);
   fseek(fsource, 6, SEEK_CUR);
   fread(&track->mode, 4, 1, fsource);
   fseek(fsource, 12, SEEK_CUR);
   fread(&track->start_lba, 4, 1, fsource);
   fread(&track->total_length, 4, 1, fsource);
   fseek(fsource, 16, SEEK_CUR);
   fread(&track->sector_size_value, 4, 1, fsource);

   switch(track->sector_size_value)
   {
      case 0 : track->sector_size = 2048; break;
      case 1 : track->sector_size = 2336; break;
      case 2 : track->sector_size = 2352; break;
      case 4 : track->sector_size = 2448; break;
      default: printf("Unsupported sector size. value %ld\n", track->sector_size_value);
      	break;
   }

   if (track->mode > 2) printf( "Unsupported format: Track mode not supported");

   fseek(fsource, 29, SEEK_CUR);
   if (image->version != CDI_V2)
   {
      fseek(fsource, 5, SEEK_CUR);
      fread(&temp_value, 4, 1, fsource);
      if (temp_value == 0xffffffff)
         fseek(fsource, 78, SEEK_CUR); // extra data (DJ 3.00.780 and up)
   }
}

void cdi_CreateToc(char * f)
{
	//clear structs to 0xFF :)
	memset(Track,0xFF,sizeof(Track));
	memset(&cdi_ses,0xFF,sizeof(cdi_ses));
	memset(&cdi_toc,0xFF,sizeof(cdi_toc));

	core_file* fsource=core_fopen(f);

	if (!fsource)
		return;

	image_s image = { 0 };
	track_s track = { 0 };

	CDI_init(fsource,&image,f);

	CDI_get_sessions(fsource,&image);

	image.remaining_sessions = image.sessions;


	printf("\n--GD toc info start--\n");
	bool ft=true, CD_M2=false,CD_M1=false,CD_DA=false;

	cdi_toc.FistTrack=1;
	u32 last_FAD=0;
	u32 TrackOffset=0;
	int _track=0;
	u32 ses_count=0;

	while(image.remaining_sessions > 0)
	{
		ft=true;
		image.global_current_session++;

		CDI_get_tracks (fsource, &image);

		image.header_position = core_ftell(fsource);

		if (image.tracks == 0){
			printf("Detected open disc\n");
			CDI_skip_next_session (fsource, &image);
			image.remaining_sessions--;
			continue;
		}

		ses_count++;

		image.remaining_tracks = image.tracks;

		while(image.remaining_tracks > 0)
		{
			track.global_current_track++;
			track.number = image.tracks - image.remaining_tracks + 1;

			CDI_read_track (fsource, &image, &track);

			image.header_position = core_ftell(fsource);

			printf("Saving  \n");
			printf("Track: %2d  \n",track.global_current_track);
			printf("Type: \n");
			switch(track.mode)
			{
			case 0 : printf("Audio/\n"); break;
			case 1 : printf("Mode1/\n"); break;
			case 2 :
			default: printf("Mode2/\n"); break;
			}
			printf("%d  \n",track.sector_size);
			
			printf("Pregap: %-3ld  \n",track.pregap_length);
			printf("Size: %-6ld  \n",track.length);
			printf("LBA: %-6ld  \n",track.start_lba);

			if (ft)
			{
				ft=false;
				cdi_ses.SessionFAD[ses_count-1]=track.pregap_length + track.start_lba;
				cdi_ses.SessionStart[ses_count-1]=track.global_current_track;
			}

			if (track.mode==2)
				CD_M2=true;
			if (track.mode==1)
				CD_M1=true;
			if (track.mode==0)
				CD_DA=true;



			cdi_toc.tracks[_track].Addr=1;//hmm is that ok ?
			cdi_toc.tracks[_track].Session=ses_count-1;
			cdi_toc.tracks[_track].FAD=track.start_lba+track.pregap_length;
			cdi_toc.tracks[_track].Control=track.mode==0?0:4;

			Track[_track].SectorSize= track.start_lba+track.pregap_length + track.length-1;

			{
				if (track.total_length < track.length + track.pregap_length)
				{
					printf("This track seems truncated. Skipping...");
					core_fseek(fsource, track.position, SEEK_SET);
					core_fseek(fsource, track.total_length, SEEK_CUR);
					Track[_track].Offset= core_ftell(fsource);
				}
				else
				{
					core_fseek(fsource, track.position, SEEK_SET);

					core_fseek(fsource, track.total_length * track.sector_size, SEEK_CUR);

					Track[_track].Offset= core_ftell(fsource);
				}
			}

			core_fseek(fsource, image.header_position, SEEK_SET);

			_track++;
			image.remaining_tracks--;
		}

		CDI_skip_next_session (fsource, &image);

		image.remaining_sessions--;
	}


	if ((CD_M1==true) && (CD_DA==false) && (CD_M2==false))
		cdi_Disctype = CdRom;
	else if (CD_M2)
		cdi_Disctype = CdRom_XA;
	else if (CD_DA && CD_M1) 
		cdi_Disctype = CdRom_Extra;
	else
		cdi_Disctype=CdRom;//hmm?

	cdi_ses.SessionCount=ses_count;
	//cdi_ses.SessionsEndFAD=pstToc->dwOuterLeadOut;
	//cdi_toc.LeadOut.FAD=pstToc->dwOuterLeadOut;
	cdi_toc.LeadOut.Addr=0;
	cdi_toc.LeadOut.Control=0;
	cdi_toc.LeadOut.Session=0;

	printf("Disc Type = %d\n",cdi_Disctype);
	TrackCount=_track;
	cdi_toc.LastTrack=_track;
	
	printf("--GD toc info end--\n\n");
}

//HMODULE pfctoc_mod=NULL;
bool cdi_init(wchar* file_)
{
	char file[512];
	strcpy(file,file_);
	#undef FILE
	FILE* fp_cdi=fopen(file,"rb");
	
	if (!fp_cdi)
		return false;

	size_t len=strlen(file);
	if (len>4)
	{
		if (strcmp( &file[len-4],".cdi")==0)
		{
			cdi_CreateToc(file);
			return true;
		}
	}

	return false;
}

void cdi_term()
{/*
	if (pstToc)
		PfcFreeToc(pstToc);
	if (pfctoc_mod)
		FreeLibrary(pfctoc_mod);
	pstToc=0;
	pfctoc_mod=0;*/
}

u32 cdi_DriveGetDiscType()
{
	return cdi_Disctype;
}
void cdi_DriveGetTocInfo(TocInfo* toc,DiskArea area)
{
	verify(area==SingleDensity);
	memcpy(toc,&cdi_toc,sizeof(TocInfo));
}
void cdi_GetSessionsInfo(SessionInfo* sessions)
{
	memcpy(sessions,&cdi_ses,sizeof(SessionInfo));
}
