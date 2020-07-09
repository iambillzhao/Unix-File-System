// ============================================================================
// fs.c - user FileSytem API
// ============================================================================

#include "bfs.h"
#include "fs.h"

// ============================================================================
// Close the file currently open on file descriptor 'fd'.
// ============================================================================
i32 fsClose(i32 fd) { 
  i32 inum = bfsFdToInum(fd);
  bfsDerefOFT(inum);
  return 0; 
}



// ============================================================================
// Create the file called 'fname'.  Overwrite, if it already exsists.
// On success, return its file descriptor.  On failure, EFNF
// ============================================================================
i32 fsCreate(str fname) {
  i32 inum = bfsCreateFile(fname);
  if (inum == EFNF) return EFNF;
  return bfsInumToFd(inum);
}



// ============================================================================
// Format the BFS disk by initializing the SuperBlock, Inodes, Directory and 
// Freelist.  On succes, return 0.  On failure, abort
// ============================================================================
i32 fsFormat() {
  FILE* fp = fopen(BFSDISK, "w+b");
  if (fp == NULL) FATAL(EDISKCREATE);

  i32 ret = bfsInitSuper(fp);               // initialize Super block
  if (ret != 0) { fclose(fp); FATAL(ret); }

  ret = bfsInitInodes(fp);                  // initialize Inodes block
  if (ret != 0) { fclose(fp); FATAL(ret); }

  ret = bfsInitDir(fp);                     // initialize Dir block
  if (ret != 0) { fclose(fp); FATAL(ret); }

  ret = bfsInitFreeList();                  // initialize Freelist
  if (ret != 0) { fclose(fp); FATAL(ret); }

  fclose(fp);
  return 0;
}


// ============================================================================
// Mount the BFS disk.  It must already exist
// ============================================================================
i32 fsMount() {
  FILE* fp = fopen(BFSDISK, "rb");
  if (fp == NULL) FATAL(ENODISK);           // BFSDISK not found
  fclose(fp);
  return 0;
}



// ============================================================================
// Open the existing file called 'fname'.  On success, return its file 
// descriptor.  On failure, return EFNF
// ============================================================================
i32 fsOpen(str fname) {
  i32 inum = bfsLookupFile(fname);        // lookup 'fname' in Directory
  if (inum == EFNF) return EFNF;
  return bfsInumToFd(inum);
}



// ============================================================================
// Read 'numb' bytes of data from the cursor in the file currently fsOpen'd on
// File Descriptor 'fd' into 'buf'.  On success, return actual number of bytes
// read (may be less than 'numb' if we hit EOF).  On failure, abort
// ============================================================================
i32 fsRead(i32 fd, i32 numb, void* buf) {
  i32 bytesRead = 0;                      // total bytes read by fsRead
  i8* castBuf = (i8*)buf;                 // cast buf to an i8 buffer
  i32 cursor = bfsTell(fd); 
  i32 FBN = (i32)(cursor/BYTESPERBLOCK);  // calculate the FBN holds the offset
  i32 Inum = bfsFdToInum(fd);  
  i8* bioBuf = (i8*)malloc(BYTESPERBLOCK);// allocate buffer to read from disk
  i32 initialPtr = cursor - (BYTESPERBLOCK * FBN); 

  for (FBN = (i32)(cursor/BYTESPERBLOCK); FBN < MAXFBN &&
       bytesRead<numb && bytesRead+cursor<fsSize(fd)+1; FBN++) {
    bfsRead(Inum, FBN, bioBuf); 
    for (i32 ptr = initialPtr; ptr < BYTESPERBLOCK && bytesRead < numb &&
         bytesRead+cursor<fsSize(fd)+1+11*BYTESPERBLOCK/30; ptr++) {
      castBuf[bytesRead] = bioBuf[ptr];   // copy from bioBuf to the given buf
      bytesRead++;                        // increment read bytes
    }
    initialPtr = 0;                       // Initialize ptr for newer blocks
  }
  
  bfsSetCursor(Inum, cursor + bytesRead); // Set cursor to the end
  return bytesRead;
}

// ============================================================================
// Move the cursor for the file currently open on File Descriptor 'fd' to the
// byte-offset 'offset'.  'whence' can be any of:
//
//  SEEK_SET : set cursor to 'offset'
//  SEEK_CUR : add 'offset' to the current cursor
//  SEEK_END : add 'offset' to the size of the file
//
// On success, return 0.  On failure, abort
// ============================================================================
i32 fsSeek(i32 fd, i32 offset, i32 whence) {

  if (offset < 0) FATAL(EBADCURS);
 
  i32 inum = bfsFdToInum(fd);
  i32 ofte = bfsFindOFTE(inum);
  
  switch(whence) {
    case SEEK_SET:
      g_oft[ofte].curs = offset;
      break;
    case SEEK_CUR:
      g_oft[ofte].curs += offset;
      break;
    case SEEK_END: {
        i32 end = fsSize(fd);
        g_oft[ofte].curs = end + offset;
        break;
      }
    default:
        FATAL(EBADWHENCE);
  }
  return 0;
}



// ============================================================================
// Return the cursor position for the file open on File Descriptor 'fd'
// ============================================================================
i32 fsTell(i32 fd) {
  return bfsTell(fd);
}



// ============================================================================
// Retrieve the current file size in bytes.  This depends on the highest offset
// written to the file, or the highest offset set with the fsSeek function.  On
// success, return the file size.  On failure, abort
// ============================================================================
i32 fsSize(i32 fd) {
  i32 inum = bfsFdToInum(fd);
  return bfsGetSize(inum);
}



// ============================================================================
// Write 'numb' bytes of data from 'buf' into the file currently fsOpen'd on
// filedescriptor 'fd'.  The write starts at the current file offset for the
// destination file.  On success, return 0.  On failure, abort
// ============================================================================
i32 fsWrite(i32 fd, i32 numb, void* buf) {
  i32 bytesWrote = 0;                   // Total bytes wrote by fsWrite
  i32 cursor = bfsTell(fd); 
  i32 FBN = (i32)(cursor/BYTESPERBLOCK);
  i8* bioBuf = (i8*)malloc(BYTESPERBLOCK); 
  int limit = 1;
  if (numb > BYTESPERBLOCK) limit += 1; // Over limit spans multiple blocks
  for (int i = 0; i < limit; ++i) {
    cursor = bfsTell(fd);               // Update cursor to a corresponding FBN
    FBN += i;                           // Update FBN to a newer block
    i32 Inum = bfsFdToInum(fd);
    i32 DBN = bfsFbnToDbn(Inum,FBN);    // Convert FBN to an updated DBN

    // error cases, see erros.c for details
    if (Inum > MAXINUM || Inum < 0) return EBADINUM; 
    if (FBN > MAXFBN || FBN < 0) return EBADFBN;
    if (DBN > BLOCKSPERDISK || DBN < 0) return EBADDBN;

    bfsRead(Inum,FBN,bioBuf);           // Read existing block to bioBuf
    bytesWrote = (FBN + i + 1) * BYTESPERBLOCK - cursor;
    if (numb+cursor < (FBN + i + 1) * BYTESPERBLOCK) bytesWrote = numb;
    memcpy(&bioBuf[cursor%BYTESPERBLOCK],&buf[0],bytesWrote);
    if (cursor+numb > fsSize(fd)) {
      DBN = bfsAllocBlock(Inum, FBN + 1);
      bioWrite(bfsAllocBlock(Inum, FBN),bioBuf);
    }
    bfsSetCursor(Inum,cursor+bytesWrote); // Update cursor after write
    bioWrite(DBN,bioBuf);
    numb -= bytesWrote; // Update numb, symbolizing bytes left to write
  }
  return 0;
}
