/*
 * Copyright (C) 2013 - Jérémie Galarneau <jeremie.galarneau@efficios.com>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License, version 2 only, as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program; if not, write to the Free Software Foundation, Inc., 51
 * Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

#define _GNU_SOURCE
#include <assert.h>
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <inttypes.h>
#include <dirent.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>

#include <common/defaults.h>
#include <common/error.h>
#include <common/macros.h>
#include <common/utils.h>
#include <lttng/lttng-error.h>
#include <libxml/parser.h>
#include <libxml/valid.h>
#include <libxml/xmlschemas.h>

#include "config.h"
#include "config-internal.h"

struct handler_filter_args {
	const char* section;
	config_entry_handler_cb handler;
	void *user_data;
};

struct session_config_validation_ctx {
	xmlSchemaParserCtxtPtr parser_ctx;
	xmlSchemaPtr schema;
	xmlSchemaValidCtxtPtr schema_validation_ctx;
};

const char * const config_str_yes = "yes";
const char * const config_str_true = "true";
const char * const config_str_on = "on";
const char * const config_str_no = "no";
const char * const config_str_false = "false";
const char * const config_str_off = "off";
const char * const config_xml_encoding = "UTF-8";
/* Size of the encoding's largest character */
const size_t config_xml_encoding_bytes_per_char = 2;
const char * const config_xml_indent_string = "\t";
const char * const config_xml_true = "true";
const char * const config_xml_false = "false";

static int config_entry_handler_filter(struct handler_filter_args *args,
		const char *section, const char *name, const char *value)
{
	int ret = 0;
	struct config_entry entry = { section, name, value };

	assert(args);

	if (!section || !name || !value) {
		ret = -EIO;
		goto end;
	}

	if (args->section) {
		if (strcmp(args->section, section)) {
			goto end;
		}
	}

	ret = args->handler(&entry, args->user_data);
end:
	return ret;
}

LTTNG_HIDDEN
int config_get_section_entries(const char *override_path, const char *section,
		config_entry_handler_cb handler, void *user_data)
{
	int ret = 0;
	FILE *config_file = NULL;
	struct handler_filter_args filter = { section, handler, user_data };

	if (override_path) {
		config_file = fopen(override_path, "r");
		if (config_file) {
			DBG("Loaded daemon configuration file at %s",
				override_path);
		} else {
			ERR("Failed to open daemon configuration file at %s",
				override_path);
			ret = -ENOENT;
			goto end;
		}
	} else {
		char *path = utils_get_home_dir();

		/* Try to open the user's daemon configuration file */
		if (path) {
			ret = asprintf(&path, DEFAULT_DAEMON_HOME_CONFIGPATH, path);
			if (ret < 0) {
				goto end;
			}

			ret = 0;
			config_file = fopen(path, "r");
			if (config_file) {
				DBG("Loaded daemon configuration file at %s", path);
			}

			free(path);
		}

		/* Try to open the system daemon configuration file */
		if (!config_file) {
			config_file = fopen(DEFAULT_DAEMON_HOME_CONFIGPATH, "r");
		}
	}

	if (!config_file) {
		DBG("No daemon configuration file found.");
		goto end;
	}

	ret = ini_parse_file(config_file,
			(ini_entry_handler) config_entry_handler_filter, (void *) &filter);

end:
	return ret;
}

LTTNG_HIDDEN
int config_parse_value(const char *value)
{
	int i, ret = 0;
	char *endptr, *lower_str;
	size_t len;
	unsigned long v;

	len = strlen(value);
	if (!len) {
		ret = -1;
		goto end;
	}

	v = strtoul(value, &endptr, 10);
	if (endptr != value) {
		ret = v;
		goto end;
	}

	lower_str = zmalloc(len + 1);
	if (!lower_str) {
		PERROR("zmalloc");
		ret = -errno;
		goto end;
	}

	for (i = 0; i < len; i++) {
		lower_str[i] = tolower(value[i]);
	}

	if (!strcmp(lower_str, config_str_yes) ||
		!strcmp(lower_str, config_str_true) ||
		!strcmp(lower_str, config_str_on)) {
		ret = 1;
	} else if (!strcmp(lower_str, config_str_no) ||
		!strcmp(lower_str, config_str_false) ||
		!strcmp(lower_str, config_str_off)) {
		ret = 0;
	} else {
		ret = -1;
	}

	free(lower_str);
end:
	return ret;
}

/* Returns a xmlChar string which must be released using xmlFree() */
static xmlChar *encode_string(const char *in_str)
{
	xmlChar *out_str = NULL;
	xmlCharEncodingHandlerPtr handler;
	int out_len;
	int ret;
	int in_len;

	if (!in_str) {
		goto end;
	}

	handler = xmlFindCharEncodingHandler(
		config_xml_encoding);
	assert(handler);

	in_len = strlen(in_str);
	out_len = (in_len * 2) + 1;
	out_str = xmlMalloc(out_len);
	if (!out_str) {
		goto end;
	}

	ret = handler->input(out_str, &out_len, (const xmlChar *) in_str,
		&in_len);
	if (ret < 0) {
		xmlFree(out_str);
		out_str = NULL;
		goto end;
	} else {
		xmlChar *realloced_out = xmlRealloc(out_str, out_len + 1);
		out_str = realloced_out ? realloced_out : out_str;
	}

	/* out_len is now the size of out_str */
	out_str[out_len] = '\0';
end:
	return out_str;
}

LTTNG_HIDDEN
struct config_writer *config_writer_create(int fd_output)
{
	int ret = 0;
	struct config_writer *writer;
	xmlOutputBufferPtr buffer;

	writer = zmalloc(sizeof(struct config_writer));
	if (!writer) {
		PERROR("zmalloc");
		goto end;
	}

	buffer = xmlOutputBufferCreateFd(fd_output, NULL);
	if (!buffer) {
		ret = -ENOMEM;
		goto error_destroy;
	}

	writer->writer = xmlNewTextWriter(buffer);
	ret = xmlTextWriterStartDocument(writer->writer, NULL,
		config_xml_encoding, NULL);
	if (ret < 0) {
		goto error_destroy;
	}

	ret = xmlTextWriterSetIndentString(writer->writer,
		BAD_CAST config_xml_indent_string);
	if (ret)  {
		goto error_destroy;
	}

	ret = xmlTextWriterSetIndent(writer->writer, 1);
	if (ret)  {
		goto error_destroy;
	}

end:
	return writer;
error_destroy:
	config_writer_destroy(writer);
	return NULL;
}

LTTNG_HIDDEN
int config_writer_destroy(struct config_writer *writer)
{
	int ret = 0;

	if (!writer) {
		ret = -EINVAL;
		goto end;
	}

	if (xmlTextWriterEndDocument(writer->writer) < 0) {
		WARN("Could not close XML document");
		ret = -EIO;
	}

	if (writer->writer) {
		xmlFreeTextWriter(writer->writer);
	}

	free(writer);
end:
	return ret;
}

LTTNG_HIDDEN
int config_writer_open_element(struct config_writer *writer,
	const char *element_name)
{
	int ret;
	xmlChar *encoded_element_name;

	if (!writer || !writer->writer || !element_name || !element_name[0]) {
		ret = -1;
		goto end;
	}

	encoded_element_name = encode_string(element_name);
	if (!encoded_element_name) {
		ret = -1;
		goto end;
	}

	ret = xmlTextWriterStartElement(writer->writer, encoded_element_name);
	xmlFree(encoded_element_name);
end:
	return ret > 0 ? 0 : ret;
}

LTTNG_HIDDEN
int config_writer_close_element(struct config_writer *writer)
{
	int ret;

	if (!writer || !writer->writer) {
		ret = -1;
		goto end;
	}

	ret = xmlTextWriterEndElement(writer->writer);
end:
	return ret > 0 ? 0 : ret;
}

LTTNG_HIDDEN
int config_writer_write_element_unsigned_int(struct config_writer *writer,
		const char *element_name, uint64_t value)
{
	int ret;
	xmlChar *encoded_element_name;

	if (!writer || !writer->writer || !element_name || !element_name[0]) {
		ret = -1;
		goto end;
	}

	encoded_element_name = encode_string(element_name);
	if (!encoded_element_name) {
		ret = -1;
		goto end;
	}

	ret = xmlTextWriterWriteFormatElement(writer->writer,
		encoded_element_name, "%" PRIu64, value);
	xmlFree(encoded_element_name);
end:
	return ret > 0 ? 0 : ret;
}

LTTNG_HIDDEN
int config_writer_write_element_signed_int(struct config_writer *writer,
		const char *element_name, int64_t value)
{
	int ret;
	xmlChar *encoded_element_name;

	if (!writer || !writer->writer || !element_name || !element_name[0]) {
		ret = -1;
		goto end;
	}

	encoded_element_name = encode_string(element_name);
	if (!encoded_element_name) {
		ret = -1;
		goto end;
	}

	ret = xmlTextWriterWriteFormatElement(writer->writer,
		encoded_element_name, "%" PRIi64, value);
	xmlFree(encoded_element_name);
end:
	return ret > 0 ? 0 : ret;
}

LTTNG_HIDDEN
int config_writer_write_element_bool(struct config_writer *writer,
		const char *element_name, int value)
{
	return config_writer_write_element_string(writer, element_name,
		value ? config_xml_true : config_xml_false);
}

LTTNG_HIDDEN
int config_writer_write_element_string(struct config_writer *writer,
		const char *element_name, const char *value)
{
	int ret;
	xmlChar *encoded_element_name = NULL;
	xmlChar *encoded_value = NULL;

	if (!writer || !writer->writer || !element_name || !element_name[0] ||
		!value) {
		ret = -1;
		goto end;
	}

	encoded_element_name = encode_string(element_name);
	if (!encoded_element_name) {
		ret = -1;
		goto end;
	}

	encoded_value = encode_string(value);
	if (!encoded_value) {
		ret = -1;
		goto end;
	}

	ret = xmlTextWriterWriteElement(writer->writer, encoded_element_name,
		encoded_value);
end:
	xmlFree(encoded_element_name);
	xmlFree(encoded_value);
	return ret > 0 ? 0 : ret;
}

static
void xml_error_handler(void *ctx, const char *format, ...)
{
	char *errMsg;
	va_list args;
	int ret;

	va_start(args, format);
	ret = vasprintf(&errMsg, format, args);
	if (ret) {
		ERR("String allocation failed in xml error handler");
		return;
	}
	va_end(args);

	fprintf(stderr, "XML Error: %s", errMsg);
	free(errMsg);
}

static
void fini_session_config_validation_ctx(
	struct session_config_validation_ctx *ctx)
{
	if (ctx->parser_ctx) {
		xmlSchemaFreeParserCtxt(ctx->parser_ctx);
	}

	if (ctx->schema) {
		xmlSchemaFree(ctx->schema);
	}

	if (ctx->schema_validation_ctx) {
		xmlSchemaFreeValidCtxt(ctx->schema_validation_ctx);
	}

	memset(ctx, 0, sizeof(struct session_config_validation_ctx));
}

static
int init_session_config_validation_ctx(
	struct session_config_validation_ctx *ctx)
{
	int ret = 0;

	ctx->parser_ctx = xmlSchemaNewParserCtxt(
		DEFAULT_SESSION_CONFIG_XSD_PATH);
	if (!ctx->parser_ctx) {
		ERR("XSD parser context creation failed");
		ret = -LTTNG_ERR_LOAD_INVALID_CONFIG;
		goto end;
	}
	xmlSchemaSetParserErrors(ctx->parser_ctx, xml_error_handler,
		xml_error_handler, NULL);

	ctx->schema = xmlSchemaParse(ctx->parser_ctx);
	if (!ctx->schema) {
		ERR("XSD parsing failed");
		ret = -LTTNG_ERR_LOAD_INVALID_CONFIG;
		goto end;
	}

	ctx->schema_validation_ctx = xmlSchemaNewValidCtxt(ctx->schema);
	if (!ctx->schema_validation_ctx) {
		ERR("XSD validation context creation failed");
		ret = -LTTNG_ERR_LOAD_INVALID_CONFIG;
		goto end;
	}
	xmlSchemaSetValidErrors(ctx->schema_validation_ctx, xml_error_handler,
		xml_error_handler, NULL);
end:
	if (ret) {
		fini_session_config_validation_ctx(ctx);
	}

	return ret;
}

static
int load_session_from_file(const char *path, const char *session_name,
	struct session_config_validation_ctx *validation_ctx)
{
	int ret;

	ret = xmlSchemaValidateFile(validation_ctx->schema_validation_ctx,
		path, 0);
	if (ret) {
		ERR("Session configuration file validation failed");
		ret = -LTTNG_ERR_LOAD_INVALID_CONFIG;
		goto end;
	}
end:
	return ret;
}

static
int load_session_from_path(const char *path, const char *session_name,
	struct session_config_validation_ctx *validation_ctx)
{
	int ret = 0;
	struct stat sb;
	int session_found = !session_name;

	ret = stat(path, &sb);
	if (ret) {
		PERROR("stat");
		ret = -LTTNG_ERR_INVALID;
		goto end;
	}

	if (S_ISDIR(sb.st_mode)) {
		DIR *directory;
		struct dirent *entry;
		struct dirent *result;
		char *file_path = NULL;
		size_t path_len = strlen(path);

		if (path_len >= PATH_MAX) {
			ret = -LTTNG_ERR_INVALID;
			goto end;
		}

		entry = malloc(sizeof(struct dirent));
		if (!entry) {
			ret = -LTTNG_ERR_NOMEM;
			goto end;
		}

		directory = opendir(path);
		if (!directory) {
			ret = -LTTNG_ERR_LOAD_IO_FAIL;
			free(entry);
			goto end;
		}

		file_path = zmalloc(PATH_MAX);
		if (!file_path) {
			ret = -LTTNG_ERR_NOMEM;
			free(entry);
			goto end;
		}

		strcpy(file_path, path);
		if (file_path[path_len - 1] != '/') {
			file_path[path_len++] = '/';
		}

		/* Search for *.lttng files */
		while (readdir_r(directory, entry, &result) && result) {
			size_t file_name_len = strlen(result->d_name);

			if (file_name_len <=
				sizeof(DEFAULT_SESSION_CONFIG_FILE_EXTENSION)) {
				continue;
			}

			if (path_len + file_name_len > PATH_MAX) {
				continue;
			}

			if (strcmp(DEFAULT_SESSION_CONFIG_FILE_EXTENSION,
				result->d_name - sizeof(
				DEFAULT_SESSION_CONFIG_FILE_EXTENSION))) {
				continue;
			}

			strcpy(file_path + path_len, result->d_name);
			file_path[path_len + file_name_len] = '\0';

			ret = load_session_from_file(path, session_name,
				validation_ctx);
			if (session_name && !ret) {
				session_found = 1;
				break;
			}
		}

		free(entry);
		free(file_path);
	} else {
		ret = load_session_from_file(path, session_name,
			validation_ctx);
		if (ret) {
			goto end;
		} else {
			session_found = 1;
		}
	}

end:
	if (!session_found) {
		ret = -LTTNG_ERR_LOAD_SESSION_NOT_FOUND;
	}

	return ret;
}

LTTNG_HIDDEN
int config_load_session(const char *path,
		const char *session_name)
{
	int ret = 0;
	struct session_config_validation_ctx validation_ctx = {0};

	ret = init_session_config_validation_ctx(&validation_ctx);
	if (ret) {
		goto end;
	}

	if (!path) {
		/* Try home path */

		/* Try system session configuration path */
	} else {
		ret = access(path, F_OK);
		if (ret) {
			PERROR("access");
			switch (errno) {
			case ENOENT:
				ret = -LTTNG_ERR_INVALID;
				break;
			case EACCES:
				ret = -LTTNG_ERR_EPERM;
				break;
			default:
				ret = -LTTNG_ERR_UNK;
				break;
			}
			goto end;
		}

		ret = load_session_from_path(path, session_name,
			&validation_ctx);
	}
end:
	return ret;
}
