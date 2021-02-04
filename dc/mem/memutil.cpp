#include "types.h"
#include "memutil.h"
#include "sh4_mem.h"


// *FIXME* ENDIAN
u32 LoadFileToSh4Mem(u32 offset,wchar*file)
{
	FILE * fd = fopen(file, "rb");
	if (fd==NULL) {
		printf("LoadFileToSh4Mem: can't load file \"%s\" to memory , file not found\n",file);
		return 0;
	}

	u32 e_ident;
	fread(&e_ident, 1,4, fd);
	fseek(fd,SEEK_SET,0);

	if( 0x464C457F == e_ident )
	{
		fclose(fd);
		printf("!\tERROR: Loading elf is not supported (%s)\n",file);
		return 0;
	} 
	else
	{
		int toff=offset;

		int size;
		fseek(fd,0,SEEK_END);
		size=ftell(fd);
		fseek(fd,0,SEEK_SET);

		fread(&mem_b[toff],1,size,fd);
		fclose(fd);
		toff+=size;

		printf("LoadFileToSh4Mem: loaded file \"%s\" to {SysMem[%x]-SysMem[%x]}\nLoadFileToSh4Mem: file size : %d bytes\n",file,offset,toff-1,toff-offset);
		return 1;
	}
}

u32 LoadBinfileToSh4Mem(u32 offset,wchar*file)
{
	u8 CheckStr[8]={0x7,0xd0,0x8,0xd1,0x17,0x10,0x5,0xdf};/* String for checking if a binary file has an inbuilt ip.bin */
	u32 rv=0;
	rv=LoadFileToSh4Mem(0x10000, file);
		
	for (int i=0;i<8;i++)
	{
		if (ReadMem8(0x8C010000 + i+0x300)!=CheckStr[i])
			return rv;
	}
	return LoadFileToSh4Mem(0x8000, file);
}
bool LoadFileToSh4Bootrom(wchar *szFile)
{
	FILE * fd = fopen(szFile, "rb");
	if (fd==NULL) {
		printf("LoadFileToSh4Bootrom: can't load file \"%s\", file not found\n", szFile);
		return false;
	}
	fseek(fd, 0, SEEK_END);	// to end of file
	int flen = ftell(fd);	// tell file position (size)
	fseek(fd, 0, SEEK_SET);	// to beginning of file

#ifndef BUILD_DEV_UNIT
	if( flen > (BIOS_SIZE) ) {
		printf("LoadFileToSh4Bootrom: can't load file \"%s\", Too Large! size(%d bytes)\n", szFile, flen);
		return false;
	}
#else
	fseek(fd, 0x15014, SEEK_SET);
#endif

	
	size_t rd=fread(&bios_b[0], 1,flen, fd);

	printf("LoadFileToSh4Bootrom: loaded file \"%s\" ,size : %d bytes\n",szFile,rd);
	fclose(fd);
	return true;
}

bool LoadFileToSh4Flashrom(wchar *szFile)
{
	FILE * fd = fopen(szFile, "rb");
	if (fd==NULL) {
		printf("LoadFileToSh4Flashrom: can't load file \"%s\", file not found\n", szFile);
		return false;
	}
	fseek(fd, 0, SEEK_END);	// to end of file
	int flen = ftell(fd);	// tell file position (size)
	fseek(fd, 0, SEEK_SET);	// to beginning of file

	if( flen > (FLASH_SIZE) ) {
		printf("LoadFileToSh4Flashrom: can't load file \"%s\", Too Large! size(%d bytes)\n", szFile, flen);
		return false;
	}

	size_t rb=fread(&flash_b[0], 1,flen, fd);

	printf("LoadFileToSh4Flashrom: loaded file \"%s\" ,size : %d bytes\n",szFile,rb);
	fclose(fd);
	return true;
}

bool SaveSh4FlashromToFile(wchar *szFile)
{
	FILE * fd = fopen(szFile, "wb");
	if (fd==NULL) {
		printf("SaveSh4FlashromToFile: can't open file \"%s\" \n", szFile);
		return false;
	}
	
	fseek(fd, 0, SEEK_SET);	// to beginning of file

	fwrite(&flash_b[0], 1,FLASH_SIZE, fd);

	printf("SaveSh4FlashromToFile: Saved flash file \"%s\"\n",szFile);
	fclose(fd);
	return true;
}


void AddHook(u32 Addr, u16 Opcode)
{
	if (Addr==0)
		return;
	u32 Offs = (Opcode != 0x000B) ? 2 : 0;
	static const u16 RtsNOP[2] = { 0x000B, 0x0009 };
	

	if( Opcode != 0x000B )	// RTS
		WriteMem16_nommu(Addr,Opcode);
 
	WriteMem16_nommu(Addr+Offs,RtsNOP[0]);
	WriteMem16_nommu(Addr+Offs+2,RtsNOP[1]);
}


#define SYSINFO_OPCODE	((u16)0x30F1)	// SYSINFO	- 0011 0000 1111 0001

 
#define dc_bios_syscall_system		0x8C0000B0
#define dc_bios_syscall_font		0x8C0000B4
#define dc_bios_syscall_flashrom	0x8C0000B8
#define dc_bios_syscall_GDrom_misc	0x8C0000BC
#define dc_bios_syscall_resets_Misc	0x8c0000E0
#define GDROM_OPCODE	((u16)0x30F9)

u32 EnabledPatches=0;


void LoadSyscallHooks()
{
	//skip em all :)

	//if (EnabledPatches & patch_syscall_system)
		AddHook(ReadMem32(dc_bios_syscall_system),0x000B);

	//if (EnabledPatches & patch_syscall_font)
		AddHook(ReadMem32_nommu(dc_bios_syscall_font),  0x000B);

	//if (EnabledPatches & patch_syscall_flashrom)
		AddHook(ReadMem32_nommu(dc_bios_syscall_flashrom),0x000B);

	//if (EnabledPatches & patch_syscall_GDrom_misc)
		AddHook(ReadMem32_nommu(dc_bios_syscall_GDrom_misc),0x000B);



	//if (EnabledPatches & patch_syscall_GDrom_HLE)
		AddHook(0x1000, GDROM_OPCODE);		// gdrom call, reads / checks status

	//if (EnabledPatches & patch_syscall_GDrom_HLE)
		AddHook(ReadMem32_nommu(dc_bios_syscall_resets_Misc),0x000B);

	//if (EnabledPatches & patch_resets_Misc)
		AddHook(ReadMem32_nommu(dc_bios_syscall_resets_Misc),0x000B);

}

void SetPatches(/*u32 Value,u32 Mask*/)
{
	//EnabledPatches=(EnabledPatches&(~Mask))|Value;
	LoadSyscallHooks();
}