/*

   This code uses GARMIN FIT SDK V21.133.00 (https://developer.garmin.com/downloads/fit/sdk/FitSDKRelease_21.133.00.zip)
   Under the Flexible and Interoperable Data Transfer (FIT) Protocol License:
   (https://www.thisisant.com/developer/ant/licensing/flexible-and-interoperable-data-transfer-fit-protocol-license).

	Convert FIT file to CSV format.
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
   bool  is_dev;
   FIT_UINT8 num_fields;
   FIT_UINT8 num_dev_fields;
   unsigned short data_mesg_len;
   FIT_FIELD_DEF fields[0];
   FIT_DEV_FIELD_DEF dev_fields[0];
} _fit_mesg_def;

// static arguments to save passing arguments between functions call
static  FIT_UINT16 crc;                            // crc - we need to global to save argument passing
static FILE *fit_f;                                // fit file handle
static FILE *csv_f;                                // csv file handle
static unsigned char *buf;                         // read buffer
static _fit_mesg_def *mesg_type_def[FIT_HDR_TYPE_MASK+1]; // track on local message types
static _fit_fixed_mesg_def fit_fixed_mesg_def;     // fixed portion of a definition message     
static unsigned char rec_hdr;                      // record header
static int fit_data_read;                          // track how much data was read 

/****************************************************/
/* convert FIT values to string based on their type */
/****************************************************/
typedef struct {
   FIT_FIT_BASE_TYPE base_type;
   char *(*val_to_str)(void *data, unsigned char size);
} _base_type_to_string;

static char string[FIT_MAX_FIELD_SIZE*4+1];  // to allow unkown base type string

static char *int8_to_str (void *v, unsigned char size) {
	sprintf(string, "%hhd", *(char *)v);
	return string;
}

static char *uint8_to_str (void *v, unsigned char size) {
	sprintf(string, "%hhu", *(unsigned char *)v);
	return string;
}

static char *int16_to_str (void *v, unsigned char size) {
	sprintf(string, "%hd", *(short int *)v);
	return string;
}


static char *uint16_to_str (void *v, unsigned char size) {
	sprintf(string, "%hu", *(unsigned short int *)v);
	return string;
}

static char *int32_to_str (void *v, unsigned char size) {
	sprintf(string, "%ld", *(long int *)v);
	return string;
}

static char *uint32_to_str (void *v, unsigned char size) {
	sprintf(string, "%lu", *(unsigned long int *)v);
	return string;
}

static char *int64_to_str (void *v, unsigned char size) {
	sprintf(string, "%lld",*(long long *)v); 
   	return string;
}

static char *uint64_to_str (void *v, unsigned char size) {
	sprintf(string, "%llu",*(unsigned long long *)v); 
   	return string;
}

static char *string_to_str (void *v, unsigned char size) {
   // check for empty string
   char *p = v;
   if (p[0] == 0)
      strcpy(string, "NULL");
   else
      strncpy(string, p, size);
   return string;
}

static _base_type_to_string base2str[FIT_FIT_BASE_TYPE_COUNT] = {
   {FIT_FIT_BASE_TYPE_ENUM, &uint8_to_str},
   {FIT_FIT_BASE_TYPE_SINT8, &int8_to_str},
   {FIT_FIT_BASE_TYPE_UINT8, &uint8_to_str},
   {FIT_FIT_BASE_TYPE_SINT16, &int16_to_str},
   {FIT_FIT_BASE_TYPE_UINT16, &uint16_to_str},
   {FIT_FIT_BASE_TYPE_SINT32, &int32_to_str},
   {FIT_FIT_BASE_TYPE_UINT32, &uint32_to_str},
   {FIT_FIT_BASE_TYPE_STRING, &string_to_str},
   {FIT_FIT_BASE_TYPE_FLOAT32, &uint32_to_str},  // the binary representation of float does not match gcc format
   {FIT_FIT_BASE_TYPE_FLOAT64, &uint64_to_str},  // the binary representation of float does not match gcc format
   {FIT_FIT_BASE_TYPE_UINT8Z, &uint8_to_str},
   {FIT_FIT_BASE_TYPE_UINT16Z, &uint16_to_str},
   {FIT_FIT_BASE_TYPE_UINT32Z, &uint32_to_str},
   {FIT_FIT_BASE_TYPE_BYTE, &int8_to_str},
   {FIT_FIT_BASE_TYPE_SINT64, &int64_to_str},
   {FIT_FIT_BASE_TYPE_UINT64, &uint64_to_str},
   {FIT_FIT_BASE_TYPE_UINT64Z, &uint64_to_str}
};

_base_type_to_string *get_type_2str (FIT_FIT_BASE_TYPE type) {
	int i = 0;

	for (i = 0; i < FIT_FIT_BASE_TYPE_COUNT; i++) {
		if (base2str[i].base_type == type)
			return &base2str[i];
	}
	return NULL;	
}

// convert unknown value base type to string of byts values
static char *unkonwn_base_type (void *val, int size) {
   char *str = string;
   while (size) {
      sprintf(str, "%03hu/", *(unsigned char *)val);
      str += 4;
      val++;
	  size--;
   }
   return string;
}

// cleanup function 
void cleanup () {
   int i;

   fclose(fit_f);
   free(buf);
   for (i = 0; i < FIT_HDR_TYPE_MASK+1; i++) {
      if (mesg_type_def[i] != NULL)
         free(mesg_type_def[i]);
   }   
}

// read buffer from FIT file
int fit_read (void *buf, int size) {
   int i;

   if ((i = fread(buf, 1, size, fit_f)) < size) {
      fprintf(stderr, "Reading FIT file failed, read %d bytes instead of %d, %s\n", i, size, strerror(errno));
      i = -1;
   }

   crc = FitCRC_Update16(crc, buf, size);
   fit_data_read += size;

   return i;
}

// calculate overall data message len
unsigned short calc_data_mesg_len (_fit_mesg_def *fit_mesg_def) {
   unsigned short us = 0;
   int i;

   // calculate fields data sizes
   for (i = 0; i < fit_mesg_def->num_fields; i++)
      us += fit_mesg_def->fields[i].size;

   // add total device fields data sizes
   for (i = 0; i < fit_mesg_def->num_dev_fields; i++)
      us += fit_mesg_def->dev_fields[i].size;

   return us;
}

void print_data_mesg (unsigned char mesg_type) {
   int i;
   void *val_ptr;
   _base_type_to_string *base_type_p;

   fprintf(csv_f, "DATA: CT,%1d, M_TYPE,%02d,,", rec_hdr & FIT_HDR_TIME_REC_BIT, mesg_type); 
   // get offset to first field value
   val_ptr = (void *)buf;

   if (rec_hdr & FIT_HDR_TIME_REC_BIT)
      fprintf(csv_f, "%d,,",rec_hdr & FIT_HDR_TIME_OFFSET_MASK);

   for (i = 0; i < mesg_type_def[mesg_type]->num_fields; i++) {
      base_type_p = get_type_2str(mesg_type_def[mesg_type]->fields[i].base_type);
      if (base_type_p != NULL)
         fprintf(csv_f, "%s,", base_type_p->val_to_str(val_ptr, mesg_type_def[mesg_type]->fields[i].size));
      else
         fprintf(csv_f, unkonwn_base_type(val_ptr, mesg_type_def[mesg_type]->fields[i].size));    // undefined base_type

      // advance to next field value
      val_ptr += mesg_type_def[mesg_type]->fields[i].size;
   }

   if (mesg_type_def[mesg_type]->is_dev) {
      for (i = 0; i < mesg_type_def[mesg_type]->num_dev_fields; i++) {
         fprintf(csv_f, "%d:%d:%d,", mesg_type_def[mesg_type]->dev_fields[i].def_num, mesg_type_def[mesg_type]->dev_fields[i].dev_index, mesg_type_def[mesg_type]->dev_fields[i].size);         
      }
   }
   fprintf(csv_f, "\n");
}

void print_def_mesg(unsigned char mesg_type) {
   int i;

   fprintf(csv_f, "DEF: M_TYPE,%d, M_NUM,%d, FIELDS,%d, DEV_FIELDS,%d,,", mesg_type, fit_fixed_mesg_def.global_mesg_num, mesg_type_def[mesg_type]->num_fields, mesg_type_def[mesg_type]->num_dev_fields);
   for (i = 0; i < mesg_type_def[mesg_type]->num_fields; i++) {
      fprintf(csv_f, "%d,%d,%d,,", mesg_type_def[mesg_type]->fields[i].field_def_num, mesg_type_def[mesg_type]->fields[i].size, mesg_type_def[mesg_type]->fields[i].base_type);
   }
   if (mesg_type_def[mesg_type]->is_dev) {
      for (i = 0; i < mesg_type_def[mesg_type]->num_dev_fields; i++) {
         fprintf(csv_f, "%d,%d,%d,,", mesg_type_def[mesg_type]->dev_fields[i].def_num, mesg_type_def[mesg_type]->dev_fields[i].size, mesg_type_def[mesg_type]->dev_fields[i].dev_index);
      }
   }
   fprintf(csv_f, "\n");

   // printf("DEF_messgae: %d\n", mesg_type);
}

// read record definition from FIT file
_fit_mesg_def *add_new_def_mesg() {
   int alloc_size;
   int read_size;
   FIT_UINT8 num_of_dev_fields;
   unsigned char mesg_type;

   // read fit_fixed_mesg_def
   if (fit_read(&fit_fixed_mesg_def, sizeof(_fit_fixed_mesg_def)) == sizeof(_fit_fixed_mesg_def)) {
      mesg_type = rec_hdr & FIT_HDR_TYPE_MASK;
      // check if new local message type is already set, if it does, release it first
      if (mesg_type_def[mesg_type] != NULL)
         free(mesg_type_def[mesg_type]);

      // calculate size of new mesg_type_def and allocate it
      alloc_size = sizeof(_fit_mesg_def) + fit_fixed_mesg_def.num_fields * sizeof(FIT_FIELD_DEF);

      // calculate read_size
      read_size = fit_fixed_mesg_def.num_fields * sizeof(FIT_FIELD_DEF);

      // read message content (fields definitions)
      if (fit_read(buf, read_size) == read_size) {

         // allocate new mesg_type_def[]
         if ((mesg_type_def[mesg_type] = malloc(alloc_size)) == NULL) {
            fprintf(stderr, "Failed to allocate memory for mesg_type_def, %s\n", strerror(errno));
            return NULL;
         }

         // update new mesg_type_def
         mesg_type_def[mesg_type]->num_fields = fit_fixed_mesg_def.num_fields;
         mesg_type_def[mesg_type]->num_dev_fields = 0;
         mesg_type_def[mesg_type]->is_dev = false;
         memcpy(mesg_type_def[mesg_type]->fields, buf, fit_fixed_mesg_def.num_fields*sizeof(FIT_FIELD_DEF));

         if (rec_hdr & FIT_HDR_DEV_DATA_BIT) {
            // first read how many device field there are
            if (fit_read(&num_of_dev_fields, sizeof(num_of_dev_fields)) < sizeof(num_of_dev_fields))
               return NULL;

            read_size = num_of_dev_fields * sizeof(FIT_DEV_FIELD_DEF);
            if (fit_read(buf, read_size) < read_size)
               return NULL;

            // reallocate mesg_type_def to accomodate device fields
            alloc_size += read_size;
            if ((mesg_type_def[mesg_type] = realloc(mesg_type_def[mesg_type], alloc_size)) == NULL) {
               fprintf(stderr, "Failed to allocate memory for mesg_type_def, %s\n", strerror(errno));
               return NULL;           
            }
            mesg_type_def[mesg_type]->is_dev = true;
            mesg_type_def[mesg_type]->num_dev_fields = num_of_dev_fields;
            memcpy(mesg_type_def[mesg_type]->dev_fields, buf, num_of_dev_fields*sizeof(FIT_DEV_FIELD_DEF));         
         }

         // set data_mesg_len;
         mesg_type_def[mesg_type]->data_mesg_len = calc_data_mesg_len(mesg_type_def[mesg_type]);
      }
   }

   return mesg_type_def[mesg_type];
}

// print file header
void print_file_header (FIT_FILE_HDR *fit_file_header) {
   fprintf(csv_f, "FIT_PROTOCOL_VERSION, %d\n", fit_file_header->protocol_version);
   fprintf(csv_f, "FIT_PROFILE_VERSION,  %d\n", fit_file_header->profile_version);
}

int main (int argc, char *argv[]) {
   int r;                                
   unsigned short data_size;
   FIT_FILE_HDR fit_file_hdr;                         // FIT file header                   
   unsigned char mesg_type;                           // last read message type
   _fit_mesg_def *fit_mesg_def_ptr;                   // address of last message def

   // print general license note
   printf("\
******************************************************************************\n\
   fit2csv  Copyright (C) 2024  Yoram Finder\n\
   This program comes with ABSOLUTELY NO WARRANTY;\n\
   This is free software, and you are welcome to redistribute it under the\n\
   GNU License (https://www.gnu.org/licenses/) conditions;\n\
******************************************************************************\n");

   if (argc < 3) {
      fprintf(stderr, "Missing arguments\n");
      fprintf(stderr, "USAGE: fit2csv <FIT_file_name> <CSV_file_name>\n");
      return 1;
   }

   // open fit file
   if ((fit_f = fopen(argv[1], "rb")) == NULL) {
      fprintf(stderr, "Failed to open FIT file: %s, %s\n", argv[1], strerror(errno));
      return 1;
   }

   // open csvfile
   if ((csv_f = fopen(argv[2], "w")) == NULL) {
      fprintf(stderr, "Failed to open CSV file: %s, %s\n", argv[2], strerror(errno));
      return 1;
   }

   // allocate local buf
   if ((buf = malloc(FIT_MAX_MESG_SIZE)) == NULL) {
      fprintf(stderr, "Failed to allocate memory, %s\n", strerror(errno));
      fclose(fit_f);
      return 1;
   }

   // init all fit_mesg_def pointers to NULL
   memset(&mesg_type_def, 0, sizeof(mesg_type_def));

   // read fit file header, but first init header record
   memset(&fit_file_hdr, 0, sizeof(fit_file_hdr));
   if ((r = fit_read(&fit_file_hdr, FIT_FILE_HDR_SIZE)) < FIT_FILE_HDR_SIZE)
      goto done_with_error;

   // check if file is FIT
   if (memcmp(fit_file_hdr.data_type, ".FIT", 4) != 0) {
      fprintf(stderr, "Input file type is not \".FIT\"\n");
      goto done_with_error;
   }

   // check if file header CRC was set. If it does, calculate header CRC and compare
   if (fit_file_hdr.crc != 0) {
      crc = FitCRC_Calc16(&fit_file_hdr, FIT_FILE_HDR_SIZE-2);
      if (crc != fit_file_hdr.crc) {
         fprintf(stderr, "Failed file header CRC check\n");
         goto done_with_error;
      }
   }

   // print file header
   print_file_header(&fit_file_hdr);
   
   // file header crc check succeeded. now reset crc to check whole file CRC
   crc = 0;
   fit_data_read = 0;

   while ((fit_data_read < fit_file_hdr.data_size) && !feof(fit_f)) {
      // read fit record header
      if ((r = fit_read(&rec_hdr, sizeof(rec_hdr))) < sizeof(rec_hdr))
         goto done_with_error;

      // check if definition message record or data record<
      if (rec_hdr & FIT_HDR_TYPE_DEF_BIT) {
         // read definition message
         if ((fit_mesg_def_ptr = add_new_def_mesg()) == NULL)
            goto done_with_error;

         mesg_type = rec_hdr & FIT_HDR_TYPE_MASK;
         print_def_mesg(mesg_type);
      }
      else {
         // reading data message
         // check if Compressed Timestamp Header. If it doeas get mesg_type accordingly
         if (rec_hdr & FIT_HDR_TIME_REC_BIT) {
            mesg_type = (rec_hdr & FIT_HDR_TIME_TYPE_MASK) >> FIT_HDR_TIME_TYPE_SHIFT;
         }
         else
            mesg_type = rec_hdr & FIT_HDR_TYPE_MASK;

         data_size = mesg_type_def[mesg_type]->data_mesg_len;
         if ((r = fit_read(buf, data_size)) < data_size)
            goto done_with_error; 

         print_data_mesg(mesg_type); 
      }
   }

   // if we got here due to reading all data byts, check file crc
   FIT_UINT16 file_crc;
   if (!feof(fit_f)) {
      // this read must be done directly so that global CRC variable will not be updated!!
      if ((r = fread(&file_crc, 1, sizeof(file_crc), fit_f)) < sizeof(file_crc))
         goto done_with_error;

      if (crc == file_crc)
         fprintf(csv_f, "END,\nReading FIT file completed successfully\n");
      else {
         fprintf(stderr, "Failed to verify FIT file CRC\n");
         goto done_with_error;
      }

   }
   else {
      fprintf(stderr, "Faild to read FIT CRC\n");
      goto done_with_error;
   }

   //done ok;
   cleanup ();
   return 0;

   //done with error
done_with_error:
   cleanup ();
   return 1;
}

