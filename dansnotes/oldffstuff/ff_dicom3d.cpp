
// ff_dicom3d.cpp
// VoxBo I/O plug-in for DICOM format, with siemens extensions
// Copyright (c) 2003-2005 by The VoxBo Development Team

// VoxBo is free software: you can redistribute it and/or modify it
// under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
// 
// VoxBo is distributed in the hope that it will be useful, but
// WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
// General Public License for more details.
// 
// You should have received a copy of the GNU General Public License
// along with VoxBo.  If not, see <http://www.gnu.org/licenses/>.
// 
// For general information on VoxBo, including the latest complete
// source code and binary distributions, manual, and associated files,
// see the VoxBo home page at: http://www.voxbo.org/
//
// original version written by Dan Kimberg

using namespace std;

#include <stdio.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <dirent.h>
#include <errno.h>
#include <stdlib.h>
#include <unistd.h>
#include <ctype.h>
#include <sstream>
#include "vbutil.h"
#include "vbio.h"

extern "C" {

#include "dicom.h"

vf_status test_dcm3d_3D(char *buf,int bufsize,string filename);
int read_head_dcm3d_3D(Cube *cb);
int read_data_dcm3d_3D(Cube *cb);

#ifdef VBFF_PLUGIN
VBFF vbff()
#else
VBFF dcm3d_vbff()
#endif
{
  VBFF tmp;
  tmp.name="DICOM 3D";
  tmp.extension="dcm";
  tmp.signature="dcm3d";
  tmp.dimensions=3;
  tmp.version_major=vbversion_major;
  tmp.version_minor=vbversion_minor;
  tmp.test_3D=test_dcm3d_3D;
  tmp.read_head_3D=read_head_dcm3d_3D;
  tmp.read_data_3D=read_data_dcm3d_3D;
  return tmp;
}

#define SIEMENS_TAG "### ASCCONV BEGIN ###"
#define SIEMENS_TAG_END "### ASCCONV END ###"

int read_multiple_slices(Cube *cb,string pat);

vf_status
test_dcm3d_3D(char *buf,int bufsize,string filename)
{
  // struct stat st;
  glob_t gb;
  string pat=patfromname(filename);
  // if the file exists but it's too short, go home
  if (pat==filename && bufsize<200)
    return vf_no;
  // no match, go home
  if (glob(pat.c_str(),0,NULL,&gb))
    return vf_no;

  tokenlist filenames;
  glob_transfer(filenames,gb);
  globfree(&gb);

  dicominfo dci;
  // bad dicom header, go home
  if (read_dicom_header(filenames[0],dci))
    return vf_no;
  // we're mosaiced, but the file isn't just a straight filename
  if (dci.mosaicflag && filename != pat)
    return vf_no;
  // we're not mosaiced, but the file isn't a slice
  if (!dci.mosaicflag && dci.dimz!=1 && filenames.size() != dci.dimz)
    return vf_no;
  return vf_yes;
}

int
read_head_dcm3d_3D(Cube *cb)
{
  dicominfo dci;
  stringstream tmps;
  glob_t gb;
  int filecount=1;

  string fname=cb->GetFileName();
  string pat=patfromname(fname);
  if (pat != fname) {
    glob(pat.c_str(),0,NULL,&gb);
    filecount=gb.gl_pathc;
    if (filecount<1) {
      globfree(&gb);
      return 120;
    }
    fname=gb.gl_pathv[0];
    globfree(&gb);
  }

  if (read_dicom_header(fname,dci))
    return 105;

  for (int i=0; i<(int)dci.protocol.size(); i++) {
    if (dci.protocol[i]==' ')
      dci.protocol[i]='_';
  }
  dci.protocol=xstripwhitespace(dci.protocol,"_");
  transfer_dicom_header(dci,*cb);
  if (pat != fname)
    cb->dimz=filecount;

  return(0);   // no error!
}

// FIXME dicominfo needs a constructor that sets defaults

int
read_data_dcm3d_3D(Cube *cb)
{
  dicominfo dci;

  string fname=cb->GetFileName();
  string pat=patfromname(fname);
  if (pat != fname) {
    return read_multiple_slices(cb,pat);
  }

  if (read_dicom_header(cb->GetFileName(),dci))
    return 150;

  if (dci.dimx != cb->dimx || dci.dimy != cb->dimy || dci.dimz != cb->dimz)
    return 105;
  
  cb->SetVolume(dci.dimx,dci.dimy,dci.dimz,vb_short);
  if (!cb->data_valid)
    return 120;
  int volumesize=dci.dimx*dci.dimy*dci.dimz*cb->datasize;
  
  // make sure we can get all the slices (there will be blanks)
  if (dci.datasize<volumesize)
    return 130;

  FILE *ifile = fopen(cb->GetFileName().c_str(),"r");
  if (!ifile)
    return 110;
  fseek(ifile,dci.offset,SEEK_SET);
  unsigned char *newdata=new unsigned char[dci.datasize];
  if (!newdata)
    return 160;
  int cnt=fread(newdata,1,dci.datasize,ifile);
  fclose(ifile);
  if (cnt < volumesize)
    return 150;

  // de-mosaic if needed
  if (dci.mosaicflag) {
    int xoffset=0;
    int yoffset=0;
    int ind=0;
    for (int k=0; k<cb->dimz; k++) {
      if (xoffset>=dci.cols) {
        xoffset=0;
        yoffset+=dci.dimy;
      }
      // first row for this cube
      int rowstart=((yoffset*dci.cols)+(xoffset))*cb->datasize;
      rowstart+=cb->dimy*cb->datasize*dci.cols;
      for (int j=0; j<cb->dimy; j++) {
        // copy a whole row and position for the next
        memcpy(cb->data+ind,newdata+rowstart,dci.dimx*cb->datasize);
        rowstart-=dci.cols*cb->datasize;
        ind+=dci.dimx*cb->datasize;
      }
      xoffset+=dci.dimx;
    }
  }
  else {
    int rowsize=dci.dimx*cb->datasize;
    for (int j=0; j<dci.dimy; j++) {
      memcpy(cb->data+((cb->dimy-1-j)*rowsize),
	     newdata+(j*rowsize),dci.dimx*cb->datasize);
    }
  }

  // FIXME valid if what?
  if (dci.byteorder!=my_endian())
    cb->byteswap();
  cb->data_valid=1;
  return(0);   // no error!
}

// int
// read_multiple_slices(Cube *cb,string pat)
// {
//   glob_t gb;
//   dicominfo dci;
//   if (glob(pat.c_str(),0,NULL,&gb))
//     return 100;

//   tokenlist filenames;
//   glob_transfer(filenames,gb);
//   globfree(&gb);

//   if (read_dicom_header(filenames[0],dci))
//     return 120;
//   dci.dimz=filenames.size();

//   if (dci.dimx == 0 || dci.dimy == 0 || dci.dimz == 0)
//     return 105;
//   cb->SetVolume(dci.dimx,dci.dimy,dci.dimz,vb_short);
//   if (!cb->data_valid)
//     return 120;
//   int slicesize=dci.dimx*dci.dimy*cb->datasize;
//   int rowsize=dci.dimx*cb->datasize;

//   unsigned char *newdata=new unsigned char[dci.datasize];
//   if (!newdata)
//     return 150;
//   for (int i=0; i<dci.dimz; i++) {
//     // prematurely out of slices, no complaint i guess
//     if (i>filenames.size()-1)
//       break;
//     dicominfo dci2;
//     if (read_dicom_header(filenames[i],dci2))
//       continue;
//     //cout << "dicomdatasize: " << dci2.datasize << "  slicesize: " << slicesize << endl;
//     // FIXME!
//     //     if (dci2.datasize<slicesize)
//     //       continue;
//     FILE *ifile = fopen(filenames(i),"r");
//     if (!ifile)
//       continue;
//     fseek(ifile,dci2.offset,SEEK_SET);
//     // FIXME if dci2.datasize is bigger than dci.datasize, following will blow up
//     int cnt=fread(newdata,1,dci2.datasize,ifile);
//     fclose(ifile);
//     if (cnt < dci2.datasize)
//       continue;
//     // the following junk inverts the slices in y
//     for (int j=0; j<dci.dimy; j++) {
//       memcpy(cb->data+(slicesize*i)+((cb->dimy-1-j)*rowsize),
// 	     newdata+(j*rowsize),dci.dimx*cb->datasize);
//     }
//     // OLD non-inverting code: memcpy(cb->data+(slicesize*i),newdata,slicesize);
//   }
//   if (dci.byteorder!=my_endian())
//     cb->byteswap();
//   return 0;
// }

} // extern "C"