/**
 * @file emit_m.c
 *
 * This file is used to generate matlab code for lcm data description.
 * 
 * This will generate encode and decode functions which work together with the matlab functions in lcm-matlab.
 * Currently there is no support for enums and variable sized matrixes are not well tested.
 *
 * @author Georg Bremer <georg.bremer@autonomos-systems.de>
 */
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <assert.h>
#include <string.h>
#include <ctype.h>
#ifdef WIN32
#define __STDC_FORMAT_MACROS			// Enable integer types
#endif
#include <inttypes.h>

#include <sys/stat.h>
#include <sys/types.h>

#include "lcmgen.h"

#define INDENT(n) (4*(n))

#define emit_start(n, ...) do { fprintf(f, "%*s", INDENT(n), ""); fprintf(f, __VA_ARGS__); } while (0)
#define emit_continue(...) do { fprintf(f, __VA_ARGS__); } while (0)
#define emit_end(...) do { fprintf(f, __VA_ARGS__); fprintf(f, "\n"); } while (0)
#define emit(n, ...) do { fprintf(f, "%*s", INDENT(n), ""); fprintf(f, __VA_ARGS__); fprintf(f, "\n"); } while (0)

static char *
dots_to_underscores(const char *s)
{
    char *p = strdup(s);

    for (char *t=p; *t!=0; t++)
        if (*t == '.')
            *t = '_';

    return p;
}

static char *
dots_to_double_colons(const char *s)
{
    // allocate the maximum possible amount of space needed
    char* p = (char*) calloc(1, 2 * strlen(s) + 1);
    char* q = p;

    for (const char *t=s; *t!=0; t++) {
        if (*t == '.') {
            *q = ':';
            q++;
            *q = ':';
        } else
            *q = *t;
        q++;
    }

    return p;
}

static char *dots_to_slashes(const char *s)
{
    char *p = strdup(s);
    for (char *t=p; *t!=0; t++)
        if (*t == '.')
            *t = G_DIR_SEPARATOR;
    return p;
}

static void make_dirs_for_file(const char *path)
{
#ifdef WIN32
    char *dirname = g_path_get_dirname(path);
    g_mkdir_with_parents(dirname, 0755);
    g_free(dirname);
#else
    int len = strlen(path);
    for (int i = 0; i < len; i++) {
        if (path[i]=='/') {
            char *dirpath = (char *) malloc(i+1);
            strncpy(dirpath, path, i);
            dirpath[i]=0;

            mkdir(dirpath, 0755);
            free(dirpath);

            i++; // skip the '/'
        }
    }
#endif
}

// Some types do not have a 1:1 mapping from lcm types to native C
// storage types. Do not free the string pointers returned by this
// function.
static const char *map_type_name(const char *t)
{
	if (!strcmp(t,"boolean"))
        return "logical";

	if (!strcmp(t,"byte"))
        return "uint8";

	if (!strcmp(t, "int8_t"))
        return "int8";

	if (!strcmp(t, "int16_t"))
        return "int16";

	if (!strcmp(t, "uint16_t"))
        return "uint16";

	if (!strcmp(t, "int32_t"))
        return "int32";

	if (!strcmp(t, "uint32_t"))
        return "uint32";

	if (!strcmp(t, "int64_t"))
        return "int64";

	if (!strcmp(t,"float"))
        return "single";

    return dots_to_underscores (t);
}

//returns encoded size if known, -1 otherwise
static int encoded_size(const char * t)
{
	if (!strcmp(t,"boolean"))
        return 1;

	if (!strcmp(t, "byte"))
        return 1;

	if (!strcmp(t, "int8_t"))
        return 1;

	if (!strcmp(t, "int16_t"))
        return 2;

	if (!strcmp(t, "uint16_t"))
        return 2;

	if (!strcmp(t, "int32_t"))
        return 4;

	if (!strcmp(t, "uint32_t"))
        return 4;

	if (!strcmp(t, "int64_t"))
        return 8;

	if (!strcmp(t, "float"))
		return 4;

	if (!strcmp(t, "double"))
		return 8;

	return -1;
}

void setup_m_options(getopt_t *gopt)
{
    getopt_add_string (gopt, 0, "mpath",    ".",      "Path for .m files");
}

static void emit_auto_generated_warning(FILE *f)
{
    fprintf(f,
            "%% THIS IS AN AUTOMATICALLY GENERATED FILE.  DO NOT MODIFY\n"
            "%% BY HAND!!\n"
            "%%\n"
            "%% Generated by lcm-gen\n"
            "%%\n"
			"%%#eml\n"
			"%%#codegen\n");
}

static char * get_file_name(lcmgen_t * lcmgen, lcm_struct_t const * lr, char const * func)
{
	char const * tn = lr->structname->lctypename;
	char * tn_ = dots_to_slashes(tn);

    // compute the target filename
	char * file_name = g_strdup_printf("%s%s%s%s.m",
            getopt_get_string(lcmgen->gopt, "mpath"),
            strlen(getopt_get_string(lcmgen->gopt, "mpath")) > 0 ? G_DIR_SEPARATOR_S : "",
            tn_,
			func);
	free(tn_);
	return file_name;
}

#define start_file(X) \
	char * file_name = get_file_name(lcm, ls, X);\
	if (!lcm_needs_generation(lcm, ls->lcmfile, file_name)) {\
		return 0;\
	}\
	make_dirs_for_file(file_name);\
	FILE * f = fopen(file_name, "w");\
	if (!f) {\
		return -1;\
	}\
	emit_auto_generated_warning(f);\
    const char* sn = ls->structname->shortname;

#define end_file() \
	fclose(f);\
	free(file_name);\
	return 0;

static int emit_header(lcmgen_t *lcm, lcm_struct_t *ls)
{
	start_file("_new");

    // define the class
    emit(0, "function S = %s_new()", sn);
    emit(1,   "S = struct(...");

    // data members
	int const members = g_ptr_array_size(ls->members);

	for (unsigned int mx = 0; mx < members; mx++) {
		lcm_member_t *lm = (lcm_member_t *) g_ptr_array_index(ls->members, mx);
		char* lm_tnc = dots_to_double_colons(lm->type->lctypename);

		emit_start(2, "'%s', ", lm->membername);
		int const dimensions = g_ptr_array_size(lm->dimensions);
		int const primitive = lcm_is_primitive_type(lm->type->lctypename);
		if (0 == dimensions) {
			emit_continue("%s%s", map_type_name(lm_tnc) , primitive ? "(0)" : "_new()");
		} else {
			emit_continue("repmat( %s%s, [", map_type_name(lm_tnc), primitive ? "(0)" : "_new()");
			for (int dx = 0; dx < dimensions - 1; ++dx) {
				lcm_dimension_t *dim = (lcm_dimension_t *) g_ptr_array_index(lm->dimensions, dx);
				emit_continue("%s, ", dim->size);
			}
			lcm_dimension_t *dim = (lcm_dimension_t*) g_ptr_array_index(lm->dimensions, dimensions - 1);
			emit_continue("%s%s] )", dim->size, dimensions == 1 ? ", 1" : "");
		}
		emit_end("%s", (members == mx + 1 ? " );" : ",..."));
	}
	emit(0, "%%endfunction");
	emit(0, "");

//    // constants TODO
//    if (g_ptr_array_size(ls->constants) > 0) {
//        emit(1, "public:");
//        for (unsigned int i = 0; i < g_ptr_array_size(ls->constants); i++) {
//            lcm_constant_t *lc = (lcm_constant_t *) g_ptr_array_index(ls->constants, i);
//            assert(lcm_is_legal_const_type(lc->lctypename));
//
//            const char *suffix = "";
//            if (!strcmp(lc->lctypename, "int64_t"))
//              suffix = "LL";
//            char const * mapped_typename = map_type_name(lc->lctypename);
//            emit(2, "static const %-8s %s = %s%s;", mapped_typename,
//                lc->membername, lc->val_str, suffix);
//            //free(mapped_typename);
//        }
//        emit(0, "");
//    }
//	emit(9, "};");
//
//    free(tn_);
	end_file();
}


static int emit_encode(lcmgen_t *lcm, lcm_struct_t *ls)
{
	start_file("_encode");
	
    emit(0, "function [buf, pos] = %s_encode(buf, pos, maxlen, S)", sn);
    emit(1,   "hash = %s_hash();", sn);
    emit(1,   "[buf, pos] = int64_encode_nohash(buf, pos, maxlen, hash, 1);");
    emit(1,   "[buf, pos] = %s_encode_nohash(buf, pos, maxlen, S, 1);", sn);
    emit(0, "%%endfunction");
    emit(0, "");

	end_file();
}

static int emit_encoded_size(lcmgen_t *lcm, lcm_struct_t *ls)
{
	start_file("_encodedSize");

    emit(0, "function bytes = %s_encodedSize(S)", sn);
    emit(1,   "bytes = uint32(8) + %s_encodedSize_nohash(S);", sn);
    emit(0, "%%endfunction");
    emit(0, "");

	end_file();
}

static int emit_decode(lcmgen_t *lcm, lcm_struct_t *ls)
{
	start_file("_decode");

    emit(0, "function [pos, S] = %s_decode(buf, pos, maxlen, S)", sn);
	emit(1,   "hash = uint32([0, 0]);");
	emit(1,   "hash(1:2) = %s_hash();", sn);
	emit(1,   "readHash = uint32([0, 0]);");
	emit(1,   "[pos, readHash] = int64_decode_nohash(buf, pos, maxlen, readHash, 1);");
    emit(1,   "if pos < 1 || readHash(1) ~= hash(1) || readHash(2) ~= hash(2)");
    emit(2,     "pos = -1;");
    emit(1,   "else");
    emit(2,     "[pos, S] = %s_decode_nohash(buf, pos, maxlen, S, 1);", sn);
    emit(1,   "end");
    emit(0, "%%endfunction");
    emit(0, "");

	end_file();
}

static int emit_get_hash(lcmgen_t *lcm, lcm_struct_t *ls)
{
	start_file("_hash");

    emit(0, "function hash = %s_hash()", sn);
    emit(1,   "hash = uint32([0, 0]);");
    emit(1,   "persistent %s_hash_value;", sn);
    emit(1,   "if isempty(%s_hash_value)", sn);
    emit(2,     "%s_hash_value = %s_computeHash([]);", sn, sn);
    emit(1,   "end");
    emit(1,   "hash = %s_hash_value;", sn);
    emit(0, "%%endfunction");
    emit(0, "");

	end_file();
}

static int emit_compute_hash(lcmgen_t *lcm, lcm_struct_t *ls)
{
	start_file("_computeHash");

    emit(0, "function hash = %s_computeHash(parents)", sn);
	emit(1,   "parents_len = length(parents);");
    emit(1,   "parents = [parents, %zu, '%s'];", strlen(sn), sn);
    emit(0, "");
	emit(1,   "hash = hex2int64('%016"PRIx64"');", ls->hash);

    for (unsigned int m = 0; m < g_ptr_array_size(ls->members); m++) {
        lcm_member_t *lm = (lcm_member_t *) g_ptr_array_index(ls->members, m);
        if(!lcm_is_primitive_type(lm->type->lctypename)) {
            char* lm_tnc = dots_to_double_colons(lm->type->lctypename);

			emit(1, "visit = true;");
			emit(1, "ix = uint32(1);");
			emit(1, "while ix < parents_len");
			emit(2,   "p_len = uint32(parents(ix));");
			emit(2,   "if %zu == p_len && strcmp(parents(ix + 1: ix + p_len), '%s')", strlen(lm_tnc), lm_tnc);
			emit(3,     "visit = false;");
			emit(3,     "break");
			emit(2,   "end");
			emit(2,   "ix = ix + p_len + 1;");
			emit(1, "end");
			emit(1, "if visit");
			emit(2,   "hash = add_overflow(hash, %s_computeHash(parents));", lm_tnc);
			emit(1, "end");
			emit(0, "");
            free(lm_tnc);
		}
	}
	emit(1,   "%%wrap around shift");
	emit(1,   "overflowbit = bitshift(hash(2), -31);");
	emit(1,   "bigendbit = bitshift(hash(1), -31);");
	emit(1,   "hash = bitshift(hash, 1);");
	emit(1,   "hash(1) = bitor(hash(1), overflowbit);");
	emit(1,   "hash(2) = bitor(hash(2), bigendbit);");
    emit(0, "%%endfunction");
    emit(0, "");

	end_file();
}

static int emit_encode_nohash(lcmgen_t *lcm, lcm_struct_t *ls)
{
	start_file("_encode_nohash");

    emit(0, "function [buf, pos] = %s_encode_nohash(buf, pos, maxlen, S, elems)", sn);
    emit(1,   "for ix = 1:elems");
    for (unsigned int m = 0; m < g_ptr_array_size(ls->members); m++) {
        lcm_member_t *lm = (lcm_member_t *) g_ptr_array_index(ls->members, m);
		char* lm_tnc = dots_to_double_colons(lm->type->lctypename);

		int const dimensions = g_ptr_array_size(lm->dimensions);
		//emit: for each dimension
		for (int dx = 0; dx < dimensions - 1; ++dx) {
			lcm_dimension_t *dim = (lcm_dimension_t*) g_ptr_array_index(lm->dimensions, dx);
			emit(2 + dx, "for dx%d = 1:%s", dx, dim->size);
		}

		{//do
			emit_start(1 + (dimensions > 1 ? dimensions : 1), "[buf, pos] = %s_encode_nohash(buf, pos, maxlen, S(ix).%s", map_type_name(lm_tnc), lm->membername);

			if (0 == dimensions) {
				emit_end(", 1);");
			}
			else {
				emit_continue("(");
				for (int dx = 0; dx < dimensions - 1; ++dx) {
					emit_continue("dx%d,", dx);
				}
				lcm_dimension_t *dim = (lcm_dimension_t*) g_ptr_array_index(lm->dimensions, dimensions - 1);
				emit_end(":), %s);", dim->size);
			}
		}

		//end
		for (int dx = dimensions; dx > 1; --dx) {
			emit(dx, "end");
		}
	
		free(lm_tnc);
	}
    emit(1,   "end");
    emit(0, "%%endfunction");
    emit(0, "");

	end_file();
}

static int emit_encoded_size_nohash(lcmgen_t *lcm, lcm_struct_t *ls)
{
	start_file("_encodedSize_nohash");

    emit(0, "function s = %s_encodedSize_nohash(S)", sn);
    emit(1,   "s = uint32(0);");
    for (unsigned int m = 0; m < g_ptr_array_size(ls->members); m++) {
        lcm_member_t *lm = (lcm_member_t *) g_ptr_array_index(ls->members, m);
		char* lm_tnc = dots_to_double_colons(lm->type->lctypename);

		int const encsize = encoded_size(lm_tnc);
		int const dimensions = g_ptr_array_size(lm->dimensions);

		if (encsize > -1) {//known constant size
			emit_start(1, "s = s + %d", encsize);
			for (int dx = 0; dx < dimensions; ++dx) {
				lcm_dimension_t *dim = (lcm_dimension_t*) g_ptr_array_index(lm->dimensions, dx);
				emit_continue(" * %s", dim->size);
			}
			emit_end(";");
		}
		else {
			if (0 == dimensions) {
				emit(1, "s = s + %s_encodedSize_nohash(S.%s);", map_type_name(lm_tnc), lm->membername);
			}
			else {
				//emit: for each dimension
				for (int dx = 0; dx < dimensions; ++dx) {
					lcm_dimension_t *dim = (lcm_dimension_t*) g_ptr_array_index(lm->dimensions, dx);
					emit(1 + dx, "for dx%d = 1:%s", dx, dim->size);
				}

				{//do
					emit_start(1 + dimensions, "s = s + %s_encodedSize_nohash(S.%s(", map_type_name(lm_tnc), lm->membername);
					for (int dx = 0; dx < dimensions - 1; ++dx) {
						emit_continue("dx%d,", dx);
					}
					emit_end("dx%d));", dimensions - 1);
				}

				//end
				for (int dx = dimensions; dx > 0; --dx) {
					emit(dx, "end");
				}
			}
		}
	
		free(lm_tnc);
	}
    emit(0, "%%endfunction");
    emit(0, "");

	end_file();
}

static int emit_decode_nohash(lcmgen_t *lcm, lcm_struct_t *ls)
{
	start_file("_decode_nohash");

    emit(0, "function [pos, S] = %s_decode_nohash(buf, pos, maxlen, S, elems)", sn);
	emit(1,   "for ix = 1:elems");
    for (unsigned int m = 0; m < g_ptr_array_size(ls->members); m++) {
        lcm_member_t *lm = (lcm_member_t *) g_ptr_array_index(ls->members, m);
		char* lm_tnc = dots_to_double_colons(lm->type->lctypename);

		int const dimensions = g_ptr_array_size(lm->dimensions);
		if (0 == dimensions) {
			emit(2, "[pos, t] = %s_decode_nohash(buf, pos, maxlen, S(ix).%s, 1);", map_type_name(lm_tnc), lm->membername);
			emit(2, "S(ix).%s = t(1);", lm->membername);
		}
		else {
			//emit: for each dimension
			for (int dx = 0; dx < dimensions - 1; ++dx) {
				lcm_dimension_t *dim = (lcm_dimension_t*) g_ptr_array_index(lm->dimensions, dx);
				emit(2 + dx, "for dx%d = 1:%s", dx, dim->size);
			}

			{//do
				emit_start(1 + (dimensions > 1 ? dimensions : 1), "[pos, t] = %s_decode_nohash(buf, pos, maxlen, S(ix).%s(", map_type_name(lm_tnc), lm->membername);
				for (int dx = 0; dx < dimensions - 1; ++dx) {
					emit_continue("dx%d,", dx);
				}
				lcm_dimension_t *dim = (lcm_dimension_t*) g_ptr_array_index(lm->dimensions, dimensions - 1);
				emit_end(":), %s);", dim->size);

				emit_start(1 + (dimensions > 1 ? dimensions : 1), "S(ix).%s(", lm->membername);
				for (int dx = 0; dx < dimensions - 1; ++dx) {
					emit_continue("dx%d,", dx);
				}
				emit_end(":) = t(1:%s);", dim->size);
			}

			//end
			for (int dx = dimensions - 1; dx > 0; --dx) {
				emit(1 + dx, "end");
			}
		}
		free(lm_tnc);
	}
    emit(1,   "end");
    emit(0, "%%endfunction");
	end_file();
}

int emit_m(lcmgen_t *lcm)
{
    // iterate through all defined message types
    for (unsigned int i = 0; i < g_ptr_array_size(lcm->structs); i++) {
        lcm_struct_t *ls = (lcm_struct_t *) g_ptr_array_index(lcm->structs, i);

		// generate code if needed
		{
            emit_header(lcm, ls);
            emit_encode(lcm, ls);
            emit_decode(lcm, ls);
            emit_encoded_size(lcm, ls);
            emit_get_hash(lcm, ls);
//            emit(0, "const char* %s::getTypeName()", sn);
//            emit(0, "{");
//            emit(1,     "return \"%s\";", sn);
//            emit(0, "}");
//            emit(0, "");

            emit_encode_nohash(lcm, ls);
            emit_decode_nohash(lcm, ls);
            emit_encoded_size_nohash(lcm, ls);
            emit_compute_hash(lcm, ls);
        }
    }

    return 0;
}