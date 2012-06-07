/************************************************************************
* db.c
* nodau console note taker
* Copyright (C) Lisa Milne 2010-2012 <lisa@ltmnet.com>
*
* db.c is free software: you can redistribute it and/or modify it
* under the terms of the GNU General Public License as published by the
* Free Software Foundation, either version 3 of the License, or
* (at your option) any later version.
*
* db.c is distributed in the hope that it will be useful, but
* WITHOUT ANY WARRANTY; without even the implied warranty of
* MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
* See the GNU General Public License for more details.
*
* You should have received a copy of the GNU General Public License
* along with this program.  If not, see <http://www.gnu.org/licenses/>
*
* In addition, as a special exception, the copyright holder (Lisa Milne)
* gives permission to link the code of this release of nodau with the
* OpenSSL project's "OpenSSL" library (or with modified versions of it
* that use the same license as the "OpenSSL" library), and distribute
* the linked executables. You must obey the GNU General Public License
* in all respects for all of the code used other than "OpenSSL". If you
* modify this file, you may extend this exception to your version of the
* file, but you are not obligated to do so. If you do not wish to do so,
* delete this exception statement from your version.
************************************************************************/

#include <stdarg.h>
/* so that getdate() works */
#define _XOPEN_SOURCE 500
/* so that asprintf works */
#define _GNU_SOURCE
#include <time.h>

#include "nodau.h"

/* convert a db string to a date string */
static char* db_gettime(char* d)
{
	time_t date = (time_t)atoi(d);
	struct tm *timeinfo = localtime(&date);
	char* tmp = asctime(timeinfo);
	tmp[strlen(tmp)-1] = '\0';
	return tmp;
}

/* convert a date string to a stamp */
static unsigned int db_getstamp(char* d)
{
	struct tm *timeinfo;
	/* if string is now, get current time */
	if (strcmp(d,"now") == 0) {
		return (unsigned int)time(NULL);
	}

	/* check datmask is set, if not create a temporary mask file */
	if (getenv("DATEMSK") == 0) {
		create_datemask();
	}

	/* get the stamp from the string */
	timeinfo = getdate(d);

	/* null means something went wrong, so print an error and return 'now' */
	if (timeinfo == NULL) {
		fprintf(stderr,"invalid date format\n");
		return db_getstamp("now");
	}

	/* convert the tm struct to a time_t */
	return mktime(timeinfo);
}

/* create the nodau table if it doesn't exist */
static int db_check()
{
	sqlite3_exec(db, "CREATE TABLE IF NOT EXISTS nodau(name VARCHAR(255), date INTEGER UNSIGNED, text TEXT, encrypted BOOLEAN DEFAULT 'false')", NULL, 0, &error_msg);

	if (error_msg) {
		fprintf(stderr,"%s\n",error_msg);
		return 1;
	}

	return 0;
}

/* get results from the database */
static sql_result *db_get(char* sql,...)
{
	/* temp storage area */
	sql_result *result;
	char dtmp[512];

	/* insert variable args to the sql statement */
	va_list ap;
	va_start(ap, sql);
	vsnprintf(dtmp, 512, sql, ap);
	va_end(ap);

	/* get a result struct */
	result = db_result_alloc();

	/* null result, return null */
	if (result == NULL)
		return NULL;

	error_msg = NULL;

	/* run the query, store the results in the result struct */
	sqlite3_get_table(db, dtmp, &result->data, &result->num_rows, &result->num_cols, &error_msg);

	/* return if there's an error message, but don't print it as
	 * that's handled elsewhere and we don't want to print it twice */
	if (error_msg)
		return NULL;

	/* return the struct */
	return result;
}

/* insert a new note */
static int db_insert(char* name, char* value)
{
	/* somewhere to put the sql */
	char sql[1024];

	/* get the current time */
	unsigned int date = (unsigned int)time(NULL);

	/* create the sql statement using the name/date/text for this note */
	sprintf(sql, "INSERT INTO nodau values('%s','%u','%s','false')", name, date, value);

	/* do it */
	return sqlite3_exec(db, sql, NULL, 0, &error_msg);
}

/* connect to the database */
int db_connect()
{
	int c;
	char* f;
	char* xdh;
	char* fl;
	error_msg = NULL;

	f = getenv("HOME");
	xdh = getenv("XDG_DATA_HOME");

	/* use XDG data directory for storing the database */
	if (!xdh || !xdh[0]) {
		asprintf(&fl,"%s/.local/share/nodau",f);
	}else{
		asprintf(&fl,"%s/nodau",xdh);
	}

	dir_create(fl);

	asprintf(&xdh,"%s/nodau.db",fl);
	free(fl);
	fl = xdh;

	/* connect */
	c = sqlite3_open_v2(fl, &db, SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE, NULL);
	free(fl);

	/* check for an error */
	if (c)
		return 1;

	c = db_check();

	/* check for an error */
	if (c)
		return 1;

	/* import from old database file */
	if (!config_read("import_old_db","false")) {
		sqlite3 *odb;
		int i;
		sql_result *res = db_result_alloc();
		asprintf(&fl,"%s/.nodau",f);
		i = sqlite3_open_v2(fl, &odb, SQLITE_OPEN_READWRITE, NULL);
		if (!i) {
			sqlite3_get_table(odb, "SELECT * FROM nodau", &res->data, &res->num_rows, &res->num_cols, &error_msg);
			if (!error_msg) {
				if (res->num_rows) {
					puts("Importing from old database\n");
					for (i=0; i<res->num_rows; i++) {
						db_insert(res->data[OCOLUMN(i,COL_NAME)],res->data[OCOLUMN(i,COL_TEXT)]);
					}
				}
				db_result_free(res);
			}
		}
		config_write("import_old_db","false");
		free(fl);
	}

	/* check the table exists and return */
	return c;
}

/* create a result struct */
sql_result *db_result_alloc()
{
	/* malloc space */
	sql_result *res = malloc(sizeof(sql_result));

	/* null means error and return */
	if (res == NULL) {
		fprintf(stderr,"allocation failure\n");
		return NULL;
	}

	/* set some default values */
	res->num_cols = 0;
	res->num_rows = 0;
	res->data = NULL;

	/* return the struct */
	return res;
}

/* free a result struct */
int db_result_free(sql_result *result)
{
	/* if null do nothing */
	if (result == NULL)
		return 1;

	/* if there's data free it */
	if (result->num_cols && result->num_rows && result->data) {
		sqlite3_free_table(result->data);
	}

	/* free the struct */
	free(result);

	/* done */
	return 0;
}

/* update an existing note */
int db_update(char* name, char* value)
{
	char sql[512];
	/* create the sql statement using the name/text for this note
	 * if it's meant to be encrypted, then crypt_key will be set */
	if (crypt_key) {
		value = note_encrypt(value,crypt_key);
		sprintf(sql, "UPDATE nodau set text='%s' , encrypted='true' WHERE name='%s'", value, name);
	}else{
		sprintf(sql, "UPDATE nodau set text='%s' , encrypted='false' WHERE name='%s'", value, name);
	}

	/* do it */
	return sqlite3_exec(db, sql, NULL, 0, &error_msg);
}

/* list notes according to search criteria */
void db_list(char* search)
{
	sql_result *res;
	int i;
	char* pref = "match";

	/* if search is null, list all */
	if (search == NULL) {
		pref = "note";
		res = db_get("SELECT * FROM nodau");

		/* nothing there */
		if (res->num_rows == 0) {
			printf("No notes to list\n");
			db_result_free(res);
			return;
		}
	}else{
		/* first try a name search */
		res = db_get("SELECT * FROM nodau WHERE name LIKE '%%%s%%'",search);

		/* if there's nothing then try a time search */
		if (res->num_rows == 0) {
			unsigned int idate;
			db_result_free(res);
			/* at time */
			if (strncmp(search,"t@",2) == 0) {
				idate = db_getstamp(search+2);
				res = db_get("SELECT * FROM nodau WHERE date = %u", idate);
			/* after time */
			}else if (strncmp(search,"t+",2) == 0) {
				idate = db_getstamp(search+2);
				res = db_get("SELECT * FROM nodau WHERE date > %u", idate);
			/* before time */
			}else if (strncmp(search,"t-",2) == 0) {
				idate = db_getstamp(search+2);
				res = db_get("SELECT * FROM nodau WHERE date < %u", idate);
			}
		}
		/* nothing there */
		if (!res || !res->num_rows || !res->num_cols) {
			printf("No notes match '%s'\n",search);
			return;
		}
	}

	/* print the list */
	for (i=0; i<res->num_rows; i++) {
		printf("%s %d: %s\n",pref,i+1,res->data[COLUMN(i,COL_NAME)]);
	}

	/* free the result */
	db_result_free(res);
}

/* open an existing note */
void db_edit(char* search)
{
	char* date;
	char* name;
	char* text;
	char* crypt;
	/* get the note by name */
	sql_result *result;
	result = db_get("SELECT * FROM nodau WHERE name = '%s'",search);

	/* nothing there */
	if (result->num_rows == 0) {
		printf("No notes match '%s'\n",search);
		db_result_free(result);
		return;
	}

	/* get the data */
	date = db_gettime(result->data[COLUMN(0,COL_DATE)]);
	name = result->data[COLUMN(0,COL_NAME)];
	text = result->data[COLUMN(0,COL_TEXT)];
	crypt = result->data[COLUMN(0,COL_CRYPT)];

	/* get the passphrase if it's encrypted */
	if (!strcmp(crypt,"true")) {
		crypt = crypt_get_key();
		text = note_decrypt(text,crypt);
		if (!text)
			return;
	}

	/* edit the note */
	edit(name, date, text);

	/* free the result */
	db_result_free(result);
}

/* show an existing note */
void db_show(char* search)
{
	char* date;
	char* name;
	char* text;
	char* crypt;
	/* get the note by name */
	sql_result *result;
	result = db_get("SELECT * FROM nodau WHERE name = '%s'",search);

	/* nothing there */
	if (result->num_rows == 0) {
		printf("No notes match '%s'\n",search);
		db_result_free(result);
		return;
	}

	/* get the data */
	date = db_gettime(result->data[COLUMN(0,COL_DATE)]);
	name = result->data[COLUMN(0,COL_NAME)];
	text = result->data[COLUMN(0,COL_TEXT)];
	crypt = result->data[COLUMN(0,COL_CRYPT)];

	/* get the passphrase if it's encrypted */
	if (!strcmp(crypt,"true")) {
		crypt = crypt_get_key();
		text = note_decrypt(text,crypt);
		if (!text)
			return;
	}

	/* display the note */
	printf("%s (%s):\n%s\n",name,date,text);

	/* free the result */
	db_result_free(result);
}

/* delete notes */
void db_del(char* search)
{
	char sql[512];
	unsigned int date = 0;
	/* try a name search */
	sql_result *result;
	result = db_get("SELECT * FROM nodau WHERE name = '%s'",search);

	/* TODO: request passphrase before deleting encrypted notes?
	 * File can be deleted without the passphrase anyway,
	 * or the db can be edited with sqlite3 shell, so is there a
	 * point to protecting from deletion? */

	/* if we got something, delete it */
	if (result->num_rows) {
		sprintf(sql, "DELETE FROM nodau WHERE name = '%s'", search);
	/* or try a delete by time at */
	}else if (strncmp(search,"t@",2) == 0) {
		date = db_getstamp(search+2);
		sprintf(sql, "DELETE FROM nodau WHERE date = %u", date);
	/* or try a delete by later than */
	}else if (strncmp(search,"t+",2) == 0) {
		date = db_getstamp(search+2);
		sprintf(sql, "DELETE FROM nodau WHERE date > %u", date);
	/* or try a delete by earlier than */
	}else if (strncmp(search,"t-",2) == 0) {
		date = db_getstamp(search+2);
		sprintf(sql, "DELETE FROM nodau WHERE date < %u", date);
	/* or print an error */
	}else{
		printf("No notes matches '%s'\n",search);
		return;
	}

	/* run the statement */
	sqlite3_exec(db, sql, NULL, 0, &error_msg);

	/* free the earlier result */
	db_result_free(result);
}

/* create a new note */
void db_new(char* search)
{
	/* search by name */
	sql_result *result;
	result = db_get("SELECT * FROM nodau WHERE name = '%s'",search);

	/* there's already a note with that name, so error and return */
	if (result->num_rows) {
		printf("There is already a note called '%s'\n",search);
		db_result_free(result);
		return;
	}

	/* free the search result */
	db_result_free(result);

	/* create the new entry */
	db_insert(search,"new entry");

	if (error_msg)
		printf("%s\n",error_msg);

	/* open for editing */
	db_edit(search);
}

/* encrypt an existing note, or create a new encrypted note */
void db_encrypt(char* search)
{
	/* search by name */
	sql_result *result;
	result = db_get("SELECT * FROM nodau WHERE name = '%s'",search);

	/* there's already a note with that name */
	if (result->num_rows) {
		char* name;
		char* text;
		char* crypt;

		/* get the data */
		name = result->data[COLUMN(0,COL_NAME)];
		text = result->data[COLUMN(0,COL_TEXT)];
		crypt = result->data[COLUMN(0,COL_CRYPT)];

		/* encrypt it if it's not already */
		if (!strcmp(crypt,"false")) {
			crypt = crypt_get_key();
			db_result_free(result);
			db_update(name,text);
		}else{
			printf("Note '%s' is already encrypted\n",search);
			db_result_free(result);
		}
		return;
	}

	/* free the search result */
	db_result_free(result);

	/* create the new entry */
	db_insert(search,"new entry");

	if (error_msg)
		printf("%s\n",error_msg);

	/* open for editing */
	db_edit(search);
}


/* decrypt an existing note or create a new encrypted note */
void db_decrypt(char* search)
{
	/* search by name */
	sql_result *result;
	result = db_get("SELECT * FROM nodau WHERE name = '%s'",search);

	/* found the note */
	if (result->num_rows) {
		char* date;
		char* name;
		char* text;
		char* crypt;

		/* get the data */
		date = db_gettime(result->data[COLUMN(0,COL_DATE)]);
		name = result->data[COLUMN(0,COL_NAME)];
		text = result->data[COLUMN(0,COL_TEXT)];
		crypt = result->data[COLUMN(0,COL_CRYPT)];

		/* decrypt it if it is encrypted */
		if (!strcmp(crypt,"true")) {
			char* t;
			crypt = crypt_get_key();
			t = note_decrypt(text,crypt);
			if (!t)
				return;
			free(crypt_key);
			crypt_key = NULL;
			db_update(search,t);
			db_result_free(result);
		}else{
			printf("Note '%s' is not encrypted\n",search);
			db_result_free(result);
		}
		return;
	}

	printf("No notes matches '%s'\n",search);
	db_result_free(result);
}
