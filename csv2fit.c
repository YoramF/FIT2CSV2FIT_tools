/*

   This code uses GARMIN FIT SDK V21.141.00 (https://developer.garmin.com/downloads/fit/sdk/FitSDKRelease_21.141.00.zip)
   Under the Flexible and Interoperable Data Transfer (FIT) Protocol License:
   (https://www.thisisant.com/developer/ant/licensing/flexible-and-interoperable-data-transfer-fit-protocol-license).

	Convert CSV file to FIT format.
   Copyright (C) <2024>  Yoram Finder

   This program is free software: you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation, either version 3 of the License, or
   (at your option) any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program.  If not, see <https://www.gnu.org/licenses/>.
*/

#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <string.h>
#include <stdbool.h>

#include <fit_example.h>
#include <fit_crc.h>

// define fixed portion of fit message record. it must be packed;
typedef struct {
   FIT_UINT8 reserved_1;
   FIT_UINT8 arch;
   FIT_MESG_NUM global_mesg_num;
   FIT_UINT8 num_fields;
} __attribute__((__packed__)) _fit_fixed_mesg_def;

typedef struct {
   FIT_UINT8 num_fields;
   FIT_UINT8 num_dev_fields;
   FIT_FIELD_DEF fields[0];
   FIT_DEV_FIELD_DEF dev_fields[0];
} _fit_mesg_def;

#define _FIT_PROTOCOL_VERSION      1
#define _FIT_PROFILE_VERSION       2
#define _FIT_DEF                   3
#define _FIT_DATA                  4
#define _FIT_END                   5
#define _FIT_NONE                  0

// static arguments to save passing arguments between functions call
static  FIT_UINT16 crc;                            // crc - we need to global to save argument passing
static FIT_UINT32 fit_data_write;                  // track how much data was read 
static _fit_mesg_def *mesg_type_def[FIT_HDR_TYPE_MASK+1]; // track on local message types 
static FILE *fit_f;                                // fit file handle
static FILE *csv_f;                                // csv file handle
static unsigned char *wbuf;                        // write buffer
static char *rbuf;                                 // read buffer
static char *delim = ":,\n";                       // CSV values delimitors
static char *token;                                // parsing token

/****************************************************/
/* convert strings to FIT values based on their type */
/****************************************************/
typedef struct {
   FIT_FIT_BASE_TYPE base_type;
   int (*str_to_val)(char *string, void *rv, int size);
} _base_type_to_value;


static int to_uint8 (char *string, void *val, int size) {
	unsigned char uc = 0;
   int s;

	s = sscanf(string, "%hhu", &uc);
	memcpy(val, &uc, sizeof(uc));
   return s;
}

static int to_int8 (char *string, void *val, int size) {
	char ch = 0;
   int s;

	s = sscanf(string, "%hhd", &ch);
	memcpy(val, &ch, sizeof(ch));
   return s;
}

static int to_int16 (char *string, void *val, int size) {
	short sh = 0;
   int s;

	s = sscanf(string, "%hd", &sh);
	memcpy(val, &sh, sizeof(sh));
   return s;
}


static int to_uint16 (char *string, void *val, int size) {
	unsigned short ush = 0;
   int s;

	s = sscanf(string, "%hu", &ush);
	memcpy(val, &ush, sizeof(ush));
   return s;
}

static int to_int32 (char *string, void *val, int size) {
	long lg = 0;
   int s;

	s = sscanf(string, "%ld", &lg);
	memcpy(val, &lg, sizeof(lg));
   return s;
}

static int to_uint32 (char *string, void *val, int size) {
	unsigned long ulg = 0;
   int s;

	s = sscanf(string, "%lu", &ulg);
	memcpy(val, &ulg, sizeof(ulg));
   return s;
}

static int to_int64 (char *string, void *val, int size) {
	long long llg = 0L;
   int s;

	s = sscanf(string, "%lld", &llg);
	memcpy(val, &llg, sizeof(llg));
   return s;
}

static int to_uint64 (char *string, void *val, int size) {
	unsigned long long ullg = 0L;
   int s;

	s = sscanf(string, "%llu", &ullg);
	memcpy(val, &ullg, sizeof(ullg));
   return s; 
}

static int to_string (char *string, void *val, int size) {
   // initialize val
   memset(val, 0, size);
   // copy string to val only of string != "NULL"
   if (strcmp("NULL", string) != 0)
      strncpy(val, string, size);
   return 1;
}


// handle unknown base type
static int unkonwn_base_type_2val (char *str, void *val, int size) {
	unsigned char rval[size];
	char *del = "/";
	int i = 0;
   char *tokloc;     // local token
   char *svloc;      // save local token location

	// reset rval;
	memset(rval, 0, sizeof(rval));

	tokloc = strtok_r(str, del, &svloc);      // use strtok_r() to keep global strtok() on right track
	while ((tokloc != NULL) && (i < size)) {
		sscanf(tokloc, "%hhu", &rval[i]);
		i++;
		tokloc = strtok_r(NULL, del, &svloc);
	}

	memcpy(val, rval, size);

   return 1;
}

static _base_type_to_value str2base[FIT_FIT_BASE_TYPE_COUNT] = {
   {FIT_FIT_BASE_TYPE_ENUM, &to_uint8},
   {FIT_FIT_BASE_TYPE_SINT8, &to_int8},
   {FIT_FIT_BASE_TYPE_UINT8, &to_uint8},
   {FIT_FIT_BASE_TYPE_SINT16, &to_int16},
   {FIT_FIT_BASE_TYPE_UINT16, &to_uint16},
   {FIT_FIT_BASE_TYPE_SINT32, &to_int32},
   {FIT_FIT_BASE_TYPE_UINT32, &to_uint32},
   {FIT_FIT_BASE_TYPE_STRING, &to_string},
   {FIT_FIT_BASE_TYPE_FLOAT32, &to_uint32},  // the binary representation of float does not match gcc format
   {FIT_FIT_BASE_TYPE_FLOAT64, &to_uint64},  // the binary representation of float does not match gcc format
   {FIT_FIT_BASE_TYPE_UINT8Z, &to_uint8},
   {FIT_FIT_BASE_TYPE_UINT16Z, &to_uint16},
   {FIT_FIT_BASE_TYPE_UINT32Z, &to_uint32},
   {FIT_FIT_BASE_TYPE_BYTE, &unkonwn_base_type_2val}, // used by developer 
   {FIT_FIT_BASE_TYPE_SINT64, &to_int64},
   {FIT_FIT_BASE_TYPE_UINT64, &to_uint64},
   {FIT_FIT_BASE_TYPE_UINT64Z, &to_uint64}
};

_base_type_to_value *get_type_2base (FIT_FIT_BASE_TYPE type) {
	int i = 0;

	for (i = 0; i < FIT_FIT_BASE_TYPE_COUNT; i++) {
		if (str2base[i].base_type == type)
			return &str2base[i];
	}
	return NULL;	
}


// get input line definition
static int get_line_def (char *tok) {

   if (tok != NULL) {
      if (strcmp (tok, "DATA") == 0)
         return _FIT_DATA;
      if (strcmp(tok, "DEF") == 0)
         return _FIT_DEF;
      if (strcmp(tok, "FIT_PROTOCOL_VERSION") == 0)
         return _FIT_PROTOCOL_VERSION;
      if (strcmp (tok, "FIT_PROFILE_VERSION") == 0)
         return _FIT_PROFILE_VERSION;
      if (strcmp (tok, "END") == 0)
         return _FIT_END;
   }

   return _FIT_NONE;
}


// write FIT file header. This function must be called at the beginnig and end of the program
bool WriteFileHeader(FIT_FILE_HDR *file_header)
{
   // header crc is the last field in file header.
	file_header->crc = FitCRC_Calc16(file_header, FIT_FILE_HDR_SIZE-sizeof(file_header->crc));
	fseek(fit_f, 0, SEEK_SET);

	if (fwrite((void *)file_header, 1, FIT_FILE_HDR_SIZE, fit_f) == FIT_FILE_HDR_SIZE)
      return true;
   else {
      fprintf(stderr, "Failed to write/update FIT file header, %s\n", strerror(errno));
      return false;
   }
}

// write data to FIT file
// update global varibles: crc, fit_data_write
int fit_write (void *buf, int size) {
   int i;

   if ((i = fwrite(buf, 1, size, fit_f)) < size) {
      fprintf(stderr, "Failed to write to FIT file, wrote %d bytes instead of %d, %s\n", i, size, strerror(errno));
      i = -1;
   }
   else {
      crc = FitCRC_Update16(crc, buf, size);
      fit_data_write += size;
   }

   return i;   
}

// process line as DATA line
// data line must include the following fields:
// CT value is a bit
// M_TYPE value is FIT_UINT8
// list of values according to the definition corresponding to M_TYPE
bool process_data_line() {
   FIT_UINT8 mesg_type;
   FIT_UINT8  time_rec_bit;
   FIT_UINT8  time_offset;
   int wbuf_off;                               // offset into write buffer
   _fit_mesg_def *mesg_def_p;
   _base_type_to_value *base_type_p;
   int i;

   // init variables
   wbuf_off = 1;     // wbuf[0] is record header;
   wbuf[0] = 0;
 
   // get compress time bit
   token = strtok(NULL, delim);
   if (strcmp(token, "CT") != 0)
      return false;
   token = strtok(NULL, delim);
   if (token == NULL)
      return false;
   to_uint8(token, &time_rec_bit, 1);

   // get message type title, M_TYPE..
   // token == "M_TYPE" otherewise -> error
   token = strtok(NULL, delim);
   if (strcmp(token, "M_TYPE") != 0)
      return false;
   // get message type value
   token = strtok(NULL, delim);
   if (token == NULL)
      return false;
   to_uint8(token, &mesg_type, 1);

   // check if mesg_type_def[mesg_type] exists
   if (mesg_type_def[mesg_type] == NULL)
      return false;
   else
      mesg_def_p = mesg_type_def[mesg_type];

   // set record header.
   // if time_rec_bit is set, next token is the time_offset value that is part of record header
   if (time_rec_bit) {
      token = strtok(NULL, delim);
      if (token == NULL)
         return false;
      to_uint8(token, &time_offset, 1);

      //set reac header
      wbuf[0] |= FIT_HDR_TIME_REC_BIT;
      wbuf[0] |= (mesg_type & 0x3) << FIT_HDR_TIME_TYPE_SHIFT;
      wbuf[0] |= time_offset & FIT_HDR_TIME_OFFSET_MASK;
   }
   else
      // simple data record
      wbuf[0] |= mesg_type & FIT_HDR_TYPE_MASK;

   // scan all field values and add their binary values to wbuf according to their types
   for (i = 0; i < mesg_def_p->num_fields; i++) {
      token = strtok(NULL, delim);
      if (token == NULL)
         return false;

      base_type_p = get_type_2base(mesg_def_p->fields[i].base_type);      
      if (base_type_p->str_to_val(token, wbuf+wbuf_off, mesg_def_p->fields[i].size) < 1)
         return false;
      
      wbuf_off += mesg_def_p->fields[i].size;
   }

   // scan all dev_field values and add their binary values to wbuf according to their types
   for (i = 0; i < mesg_def_p->num_dev_fields; i++) {
      token = strtok(NULL, delim);
      if (token == NULL)
         return false;

      base_type_p = get_type_2base(FIT_FIT_BASE_TYPE_BYTE);      
      if (base_type_p->str_to_val(token, wbuf+wbuf_off, mesg_def_p->dev_fields[i].size) < 1)
         return false;
      
      wbuf_off += mesg_def_p->dev_fields[i].size;
   }

   // write wbuf to FIT file
   if (fit_write(wbuf, wbuf_off) < wbuf_off)
      return false;

   return true;
}

// process line as MESSAGE DEFINITION line
// definition line includes the following fields:
// M_TYPE,0, M_NUM,324, FIELDS,4, DEV_FIELDS,0,,253,4,134,,2,8,13,,0,2,132,,1,1,2,,
// M_TYPE value is FIT_UINT8
// M_NUM vaue is FIT_MESG_NUM
// FIELDS value is FIT_UINT8
// DEV_FIELDS value is FIT_UINT8
// each field is FIT_FIELD_DEF
// each dev_field is FIT_DEV_FIELD_DEF
bool process_definition_line() {
   FIT_UINT8 mesg_type;
   FIT_UINT8 num_fields;
   FIT_UINT8 num_dev_fields;
   FIT_UINT8 field_member;
   FIT_MESG_NUM global_mesg_num;
   int i;
   int wbuf_off;                               // offset into write buffer
   _fit_fixed_mesg_def fit_fixed_mesg_def;     // fixed portion of a definition message
   int size;

   // init variables
   memset(&fit_fixed_mesg_def, 0, sizeof(fit_fixed_mesg_def));
   wbuf_off = 1;     // wbuf[0] is record header;
   wbuf[0] = FIT_HDR_TYPE_DEF_BIT;      // reset record header as definition

   // get message type title, M_TYPE..
   // token == "M_TYPE" otherewise -> error
   token = strtok(NULL, delim);
   if (strcmp(token, "M_TYPE") != 0)
      return false;

   // get message type value
   token = strtok(NULL, delim);
   if (token == NULL)
      return false;
   to_uint8(token, &mesg_type, 1);
   wbuf[0] |= mesg_type & FIT_HDR_TYPE_MASK;  // set message type;

   // get global message number title
   token = strtok(NULL, delim);
   if (strcmp(token, "M_NUM") != 0)
      return false;
   //get global message number value
   token = strtok(NULL, delim);
   if (token == NULL)
      return false;
   to_uint16(token, &global_mesg_num, 2);

   // read number of fields title
   token = strtok(NULL, delim);
   if (strcmp(token, "FIELDS") != 0)
      return false;

   // read number of fields value
   token = strtok(NULL, delim);
   if (token == NULL)
      return false;
   to_uint8(token, &num_fields, 1);

   // read number of dev fields number title
   token = strtok(NULL, delim);
   if (strcmp(token, "DEV_FIELDS") != 0)
      return false;

   // read number of dev fields number value
   token = strtok(NULL, delim);
   if (token == NULL)
      return false;
   to_uint8(token, &num_dev_fields, 1);   

   fit_fixed_mesg_def.arch = 0;
   fit_fixed_mesg_def.reserved_1 = 0;
   fit_fixed_mesg_def.global_mesg_num = global_mesg_num;
   fit_fixed_mesg_def.num_fields = num_fields;

   // update wbuf with fit_fixed_mesg_def record
   memcpy(wbuf+wbuf_off, &fit_fixed_mesg_def, sizeof(fit_fixed_mesg_def));
   wbuf_off += sizeof(fit_fixed_mesg_def);

   // save new message def in mesg_type_def, but first check if mesg_type is already in use
   // if it does, release it
   if (mesg_type_def[mesg_type] != NULL)
      free(mesg_type_def[mesg_type]);

   // calculate size of new mesg_type_def and allocate it
   size = sizeof(_fit_mesg_def) + num_fields * sizeof(FIT_FIELD_DEF) + num_dev_fields * sizeof(FIT_DEV_FIELD_DEF);
   if ((mesg_type_def[mesg_type] = malloc(size)) == NULL)
      return false;

   mesg_type_def[mesg_type]->num_dev_fields = num_dev_fields;
   mesg_type_def[mesg_type]->num_fields = num_fields;

   // update record header dev data flag if there are dev fields
   if (mesg_type_def[mesg_type]->num_dev_fields > 0)
      wbuf[0] |= FIT_HDR_DEV_DATA_BIT;

   // now read all fields and message fields definitions into mesg_type_def[mesg_type]
   for (i = 0; i < num_fields; i++) {
      token = strtok(NULL, delim);
      if (token == NULL)
         return false;
      to_uint8(token, &mesg_type_def[mesg_type]->fields[i].field_def_num, 1);
      token = strtok(NULL, delim);
      if (token == NULL)
         return false;
      to_uint8(token, &mesg_type_def[mesg_type]->fields[i].size, 1);
      token = strtok(NULL, delim);
      if (token == NULL)
         return false;
      to_uint8(token, &mesg_type_def[mesg_type]->fields[i].base_type, 1);
   }

   for (i = 0; i < num_dev_fields; i++) {
      token = strtok(NULL, delim);
      if (token == NULL)
         return false;
      to_uint8(token, &mesg_type_def[mesg_type]->dev_fields[i].def_num, 1);
      token = strtok(NULL, delim);
      if (token == NULL)
         return false;
      to_uint8(token, &mesg_type_def[mesg_type]->dev_fields[i].size, 1);
      token = strtok(NULL, delim);
      if (token == NULL)
         return false;
      to_uint8(token, &mesg_type_def[mesg_type]->dev_fields[i].dev_index, 1);
   }

   // update wbuf
   size = mesg_type_def[mesg_type]->num_fields*sizeof(FIT_FIELD_DEF);
   memcpy(wbuf+wbuf_off, &mesg_type_def[mesg_type]->fields, size);
   wbuf_off += size;

   // if there are dev fields add them to wbuf
   if (mesg_type_def[mesg_type]->num_dev_fields > 0) {
      size = sizeof(mesg_type_def[mesg_type]->num_dev_fields);
      memcpy(wbuf+wbuf_off, &mesg_type_def[mesg_type]->num_dev_fields, size);
      wbuf_off += size;

      size = mesg_type_def[mesg_type]->num_dev_fields*sizeof(FIT_DEV_FIELD_DEF);
      memcpy(wbuf+wbuf_off, &mesg_type_def[mesg_type]->dev_fields, size);
      wbuf_off += size;
   }

   // write wbuf to FIT file
   if (fit_write(wbuf, wbuf_off) < wbuf_off)
      return false;

   return true;
}

// cleanup function 
void cleanup () {
   int i;

   fclose(fit_f);
   fclose(csv_f);
   free(rbuf);
   free(wbuf);
   for (i = 0; i < FIT_HDR_TYPE_MASK+1; i++) {
      if (mesg_type_def[i] != NULL)
         free(mesg_type_def[i]);
   }   
}


int main (int argc, char *argv[]) {                         
   FIT_FILE_HDR fit_file_hdr;                         // FIT file header                   
   _fit_mesg_def *fit_mesg_def_ptr;                   // address of last message def
   int  line_mesg_deg;                                // csv line definition
   static unsigned char rec_hdr;                      // record header
   int line_def;                                      // CSV line definition

   // print general license note
   printf("\
******************************************************************************\n\
   csv2fit  Copyright (C) 2024  Yoram Finder\n\
   This program comes with ABSOLUTELY NO WARRANTY;\n\
   This is free software, and you are welcome to redistribute it under the\n\
   GNU License (https://www.gnu.org/licenses/) conditions;\n\
******************************************************************************\n");

   if (argc < 3) {
      fprintf(stderr, "Missing arguments\n");
      fprintf(stderr, "USAGE: csv2fit <CSV_file_name> <FIT_file_name>\n");
      return 1;
   }

   // open csv file
   if ((csv_f = fopen(argv[1], "r")) == NULL) {
      fprintf(stderr, "Failed to open CSV file: %s, %s\n", argv[1], strerror(errno));
      return 1;
   }

   // open fit file
   if ((fit_f = fopen(argv[2], "w+b")) == NULL) {
      fprintf(stderr, "Failed to open FIT file: %s, %s\n", argv[2], strerror(errno));
      fclose(csv_f);
      return 1;
   }

   // allocate local read buf
   if ((rbuf = malloc(FIT_MAX_MESG_SIZE)) == NULL) {
      fprintf(stderr, "Failed to allocate memory, %s\n", strerror(errno));
      fclose(csv_f);
      fclose(fit_f);  
      return 1;
   }

   // allocate local write buf
   if ((wbuf = malloc(FIT_MAX_MESG_SIZE)) == NULL) {
      fprintf(stderr, "Failed to allocate memory, %s\n", strerror(errno));
      fclose(fit_f);      
      fclose(csv_f);
      free(rbuf);
      return 1;
   }

   // init all fit_mesg_def pointers to NULL
   memset(&mesg_type_def, 0, sizeof(mesg_type_def));

   // write fit file header - it will be updated before file is closed!
   fit_file_hdr.header_size = FIT_FILE_HDR_SIZE;
	fit_file_hdr.profile_version = FIT_PROFILE_VERSION;
	fit_file_hdr.protocol_version = FIT_PROTOCOL_VERSION_20;
   fit_file_hdr.data_size = 0;
	memcpy((FIT_UINT8 *)&fit_file_hdr.data_type, ".FIT", 4);
   if (!WriteFileHeader(&fit_file_hdr))
      goto done_with_error;

   
   // file header crc check succeeded. now reset crc to check whole file CRC
   crc = 0;
   fit_data_write = 0;
   line_def = _FIT_NONE;

   while ((fgets(rbuf, FIT_MAX_MESG_SIZE, csv_f) != NULL) && !feof(csv_f) && (line_def != _FIT_END)) {

      token = strtok(rbuf, delim);
      line_def = get_line_def (token);

      switch (line_def) {
         case _FIT_PROTOCOL_VERSION:
            token = strtok(NULL, delim);
               if (token != NULL)
         	      to_uint8(token, &fit_file_hdr.protocol_version, 1) ;
            break;
         case _FIT_PROFILE_VERSION:
            token = strtok(NULL, delim);
               if (token != NULL)
         	      to_uint16(token, &fit_file_hdr.profile_version, 2) ;
            break;
         case _FIT_DEF:
            if (!process_definition_line()) {
               fprintf(stderr, "Error processing definition line, %s\n", rbuf);
               goto done_with_error;
            }
            break;
         case _FIT_DATA:
            if (!process_data_line()) {
               fprintf(stderr, "Error processing data line, %s\n", rbuf);
               goto done_with_error;
            }
            break;
         case _FIT_END:
            break;
        default:
      }
   }

   // check if we exit the loop due to _FIT_END. If not CSV file is not complete - exit with error
   if (line_def != _FIT_END) {
      fprintf(stderr, "CSV file must end with \"END,\" line. FIT file is not complete!\n");
      goto done_with_error;
   }

   // we got heare after reading all lines in CSV file
   fit_file_hdr.data_size = fit_data_write;

   // update file header
   if (!WriteFileHeader(&fit_file_hdr))
      goto done_with_error;

   // write crc to end of FIT file;
	fseek(fit_f, 0, SEEK_END);
   if (fwrite(&crc, 1, sizeof(crc), fit_f) < sizeof(crc)) {
      fprintf(stderr, "Failed to write CRC to fit file, %s\n", strerror(errno));
      goto done_with_error;
   }

   //done ok;
   printf("Converting CSV to FIT file completed successfully\n");
   cleanup ();
   return 0;

   //done with error
done_with_error:
   cleanup ();
   return 1;
}

