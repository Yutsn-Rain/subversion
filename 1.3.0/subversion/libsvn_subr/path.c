/*
 * paths.c:   a path manipulation library using svn_stringbuf_t
 *
 * ====================================================================
 * Copyright (c) 2000-2004 CollabNet.  All rights reserved.
 *
 * This software is licensed as described in the file COPYING, which
 * you should have received as part of this distribution.  The terms
 * are also available at http://subversion.tigris.org/license-1.html.
 * If newer versions of this license are posted there, you may use a
 * newer version instead, at your option.
 *
 * This software consists of voluntary contributions made by many
 * individuals.  For exact contribution history, see the revision
 * history and logs, available at http://subversion.tigris.org/.
 * ====================================================================
 */



#include <string.h>
#include <assert.h>

#include <apr_file_info.h>
#include <apr_lib.h>

#include "svn_string.h"
#include "svn_path.h"
#include "svn_private_config.h"         /* for SVN_PATH_LOCAL_SEPARATOR */
#include "svn_utf.h"
#include "svn_io.h"                     /* for svn_io_stat() */
#include "svn_ctype.h"

#if APR_CHARSET_EBCDIC
#include "httpd.h"
#endif

/* The canonical empty path.  Can this be changed?  Well, change the empty
   test below and the path library will work, not so sure about the fs/wc
   libraries. */
#define SVN_EMPTY_PATH ""

/* TRUE if s is the canonical empty path, FALSE otherwise */
#define SVN_PATH_IS_EMPTY(s) ((s)[0] == '\0')

/* TRUE if s,n is the platform's empty path ("."), FALSE otherwise. Can
   this be changed?  Well, the path library will work, not so sure about
   the OS! */
#define SVN_PATH_IS_PLATFORM_EMPTY(s,n) ((n) == 1 && (s)[0] == SVN_UTF8_DOT)
#if APR_CHARSET_EBCDIC
#define SVN_PATH_IS_PLATFORM_EMPTY_EBCDIC(s,n) ((n) == 1 && (s)[0] == '.')
#endif


const char *
svn_path_internal_style (const char *path, apr_pool_t *pool)
{
  if (SVN_UTF8_FSLASH != SVN_PATH_LOCAL_SEPARATOR)
    {
      char *p = apr_pstrdup (pool, path);
      path = p;

      /* Convert all local-style separators to the canonical ones. */
      for (; *p != '\0'; ++p)
        if (*p == SVN_PATH_LOCAL_SEPARATOR)
          *p = SVN_UTF8_FSLASH;
    }

  return svn_path_canonicalize (path, pool);
  /* FIXME: Should also remove trailing /.'s, if the style says so. */
}


const char *
svn_path_local_style (const char *path, apr_pool_t *pool)
{
  path = svn_path_canonicalize (path, pool);
  /* FIXME: Should also remove trailing /.'s, if the style says so. */

  /* Internally, Subversion represents the current directory with the
     empty string.  But users like to see "." . */
  if (SVN_PATH_IS_EMPTY(path))
    return SVN_UTF8_DOT_STR;

  if (SVN_UTF8_FSLASH != SVN_PATH_LOCAL_SEPARATOR)
    {
      char *p = apr_pstrdup (pool, path);
      path = p;

      /* Convert all canonical separators to the local-style ones. */
      for (; *p != '\0'; ++p)
        if (*p == SVN_UTF8_FSLASH)
          *p = SVN_PATH_LOCAL_SEPARATOR;
    }

  return path;
}



#ifndef NDEBUG
static svn_boolean_t
is_canonical (const char *path,
              apr_size_t len)
{
  return (! SVN_PATH_IS_PLATFORM_EMPTY (path, len)
          && (len <= 1 || path[len-1] != SVN_UTF8_FSLASH));
}

#if APR_CHARSET_EBCDIC
static svn_boolean_t
is_canonical_ebcdic (const char *path,
                     apr_size_t len)
{
  return (! SVN_PATH_IS_PLATFORM_EMPTY_EBCDIC (path, len)
          && (len <= 1 || path[len-1] != '/'));
}
#endif /* APR_CHARSET_EBCDIC */

#endif


char *svn_path_join (const char *base,
                     const char *component,
                     apr_pool_t *pool)
{
  apr_size_t blen = strlen (base);
  apr_size_t clen = strlen (component);
  char *path;

  assert (is_canonical (base, blen));
  assert (is_canonical (component, clen));

  /* If the component is absolute, then return it.  */
  if (*component == SVN_UTF8_FSLASH)
    return apr_pmemdup (pool, component, clen + 1);

  /* If either is empty return the other */
  if (SVN_PATH_IS_EMPTY (base))
    return apr_pmemdup (pool, component, clen + 1);
  if (SVN_PATH_IS_EMPTY (component))
    return apr_pmemdup (pool, base, blen + 1);

  if (blen == 1 && base[0] == SVN_UTF8_FSLASH)
    blen = 0; /* Ignore base, just return separator + component */

  /* Construct the new, combined path. */
  path = apr_palloc (pool, blen + 1 + clen + 1);
  memcpy (path, base, blen);
  path[blen] = SVN_UTF8_FSLASH;
  memcpy (path + blen + 1, component, clen + 1);

  return path;
}

#if APR_CHARSET_EBCDIC
char *svn_path_join_ebcdic (const char *base,
                            const char *component,
                            apr_pool_t *pool)
{
  apr_size_t blen = strlen (base);
  apr_size_t clen = strlen (component);
  char *path;

  assert (is_canonical_ebcdic (base, blen));
  assert (is_canonical_ebcdic (component, clen));

  /* If the component is absolute, then return it.  */
  if (*component == '/')
    return apr_pmemdup (pool, component, clen + 1);

  /* If either is empty return the other */
  if (SVN_PATH_IS_EMPTY (base))
    return apr_pmemdup (pool, component, clen + 1);
  if (SVN_PATH_IS_EMPTY (component))
    return apr_pmemdup (pool, base, blen + 1);

  if (blen == 1 && base[0] == '/')
    blen = 0; /* Ignore base, just return separator + component */

  /* Construct the new, combined path. */
  path = apr_palloc (pool, blen + 1 + clen + 1);
  memcpy (path, base, blen);
  path[blen] = '/';
  memcpy (path + blen + 1, component, clen + 1);

  return path;
}
#endif

char *svn_path_join_many (apr_pool_t *pool, const char *base, ...)
{
#define MAX_SAVED_LENGTHS 10
  apr_size_t saved_lengths[MAX_SAVED_LENGTHS];
  apr_size_t total_len;
  int nargs;
  va_list va;
  const char *s;
  apr_size_t len;
  char *path;
  char *p;
  svn_boolean_t base_is_empty = FALSE, base_is_root = FALSE;
  int base_arg = 0;

  total_len = strlen (base);

  assert (is_canonical (base, total_len));

  if (total_len == 1 && *base == SVN_UTF8_FSLASH)
    base_is_root = TRUE;
  else if (SVN_PATH_IS_EMPTY (base))
    {
      total_len = sizeof (SVN_EMPTY_PATH) - 1;
      base_is_empty = TRUE;
    }

  saved_lengths[0] = total_len;

  /* Compute the length of the resulting string. */

  nargs = 0;
  va_start (va, base);
  while ((s = va_arg (va, const char *)) != NULL)
    {
      len = strlen (s);

      assert (is_canonical (s, len));

      if (SVN_PATH_IS_EMPTY (s))
        continue;

      if (nargs++ < MAX_SAVED_LENGTHS)
        saved_lengths[nargs] = len;

      if (*s == SVN_UTF8_FSLASH)
        {
          /* an absolute path. skip all components to this point and reset
             the total length. */
          total_len = len;
          base_arg = nargs;
          base_is_root = len == 1;
          base_is_empty = FALSE;
        }
      else if (nargs == base_arg
               || (nargs == base_arg + 1 && base_is_root)
               || base_is_empty)
        {
          /* if we have skipped everything up to this arg, then the base
             and all prior components are empty. just set the length to
             this component; do not add a separator.  If the base is empty
             we can now ignore it. */
          if (base_is_empty)
            {
              base_is_empty = FALSE;
              total_len = 0;
            }
          total_len += len;
        }
      else
        {
          total_len += 1 + len;
        }
    }
  va_end (va);

  /* base == "/" and no further components. just return that. */
  if (base_is_root && total_len == 1)
    return apr_pmemdup (pool, SVN_UTF8_FSLASH_STR, 2);

  /* we got the total size. allocate it, with room for a NULL character. */
  path = p = apr_palloc (pool, total_len + 1);

  /* if we aren't supposed to skip forward to an absolute component, and if
     this is not an empty base that we are skipping, then copy the base
     into the output. */
  if (base_arg == 0 && ! (SVN_PATH_IS_EMPTY (base) && ! base_is_empty))
    {
      if (SVN_PATH_IS_EMPTY (base))
        memcpy(p, SVN_EMPTY_PATH, len = saved_lengths[0]);
      else
        memcpy(p, base, len = saved_lengths[0]);
      p += len;
    }

  nargs = 0;
  va_start (va, base);
  while ((s = va_arg (va, const char *)) != NULL)
    {
      if (SVN_PATH_IS_EMPTY (s))
        continue;

      if (++nargs < base_arg)
        continue;

      if (nargs < MAX_SAVED_LENGTHS)
        len = saved_lengths[nargs];
      else
        len = strlen (s);

      /* insert a separator if we aren't copying in the first component
         (which can happen when base_arg is set). also, don't put in a slash
         if the prior character is a slash (occurs when prior component
         is "/"). */
      if (p != path && p[-1] != SVN_UTF8_FSLASH)
        *p++ = SVN_UTF8_FSLASH;

      /* copy the new component and advance the pointer */
      memcpy (p, s, len);
      p += len;
    }
  va_end (va);

  *p = '\0';
  assert ((apr_size_t)(p - path) == total_len);

  return path;
}



apr_size_t
svn_path_component_count (const char *path)
{
  apr_size_t count = 0;

  assert (is_canonical (path, strlen (path)));

  while (*path)
    {
      const char *start;

      while (*path == SVN_UTF8_FSLASH)
        ++path;

      start = path;
      
      while (*path && *path != SVN_UTF8_FSLASH)
        ++path;

      if (path != start)
        ++count;
    }

  return count;
}

/* Return the length of substring necessary to encompass the entire
 * previous path segment in PATH, which should be a LEN byte string.
 *
 * A trailing slash will not be included in the returned length except
 * in the case in which PATH is absolute and there are no more
 * previous segments.
 */
static apr_size_t
previous_segment (const char *path,
                  apr_size_t len)
{
  if (len == 0)
    return 0;

  while (len > 0 && path[--len] != SVN_UTF8_FSLASH)
    ;

  if (len == 0 && path[0] == SVN_UTF8_FSLASH)
    return 1;
  else
    return len;
}

#if APR_CHARSET_EBCDIC
/* Same as previous_segment, but operates on an ebcdic encoded path.
 */
static apr_size_t
previous_segment_ebcdic (const char *path,
                         apr_size_t len)
{
  if (len == 0)
    return 0;

  while (len > 0 && path[--len] != '/')
    ;

  if (len == 0 && path[0] == '/')
    return 1;
  else
    return len;
}
#endif

void
svn_path_add_component (svn_stringbuf_t *path, 
                        const char *component)
{
  apr_size_t len = strlen (component);

  assert (is_canonical (path->data, path->len));
  assert (is_canonical (component, len));

  /* Append a dir separator, but only if this path is neither empty
     nor consists of a single dir separator already. */
  if ((! SVN_PATH_IS_EMPTY (path->data))
      && (! ((path->len == 1) && (*(path->data) == SVN_UTF8_FSLASH))))
    {
      char dirsep = SVN_UTF8_FSLASH;
      svn_stringbuf_appendbytes (path, &dirsep, sizeof (dirsep));
    }

  svn_stringbuf_appendbytes (path, component, len);
}


void
svn_path_remove_component (svn_stringbuf_t *path)
{
  assert (is_canonical (path->data, path->len));

  path->len = previous_segment(path->data, path->len);
  path->data[path->len] = '\0';
}

void
svn_path_remove_components (svn_stringbuf_t *path, apr_size_t n)
{
  while (n > 0)
    {
      svn_path_remove_component (path);
      n--;
    }
}


char *
svn_path_dirname (const char *path, apr_pool_t *pool)
{
  apr_size_t len = strlen (path);

  assert (is_canonical (path, len));

  return apr_pstrmemdup (pool, path, previous_segment(path, len));
}


#if APR_CHARSET_EBCDIC
char *
svn_path_dirname_ebcdic (const char *path, apr_pool_t *pool)
{
  apr_size_t len = strlen (path);

  assert (is_canonical_ebcdic (path, len));

  return apr_pstrmemdup (pool, path, previous_segment_ebcdic(path, len));
}
#endif


char *
svn_path_basename (const char *path, apr_pool_t *pool)
{
  apr_size_t len = strlen (path);
  apr_size_t start;

  assert (is_canonical (path, len));

  if (len == 1 && path[0] == SVN_UTF8_FSLASH)
    start = 0;
  else
    {
      start = len;
      while (start > 0 && path[start - 1] != SVN_UTF8_FSLASH)
        --start;
    }

  return apr_pstrmemdup (pool, path + start, len - start);
}


#if APR_CHARSET_EBCDIC
char *
svn_path_basename_ebcdic (const char *path, apr_pool_t *pool)
{
  apr_size_t len = strlen (path);
  apr_size_t start;

  assert (is_canonical_ebcdic (path, len));

  if (len == 1 && path[0] == '/')
    start = 0;
  else
    {
      start = len;
      while (start > 0 && path[start - 1] != '/')
        --start;
    }

  return apr_pstrmemdup (pool, path + start, len - start);
}
#endif


void
svn_path_split (const char *path,
                const char **dirpath,
                const char **base_name,
                apr_pool_t *pool)
{
  assert (dirpath != base_name);

  if (dirpath)
    *dirpath = svn_path_dirname (path, pool);

  if (base_name)
    *base_name = svn_path_basename (path, pool);
}


#if APR_CHARSET_EBCDIC
void
svn_path_split_ebcdic (const char *path,
                       const char **dirpath,
                       const char **base_name,
                       apr_pool_t *pool)
{
  assert (dirpath != base_name);

  if (dirpath)
    *dirpath = svn_path_dirname_ebcdic (path, pool);

  if (base_name)
    *base_name = svn_path_basename_ebcdic (path, pool);
}
#endif


int
svn_path_is_empty (const char *path)
{
  /* assert (is_canonical (path, strlen (path))); ### Expensive strlen */

  if (SVN_PATH_IS_EMPTY (path))
    return 1;

  return 0;
}


int
svn_path_compare_paths (const char *path1,
                        const char *path2)
{
  apr_size_t path1_len = strlen (path1);
  apr_size_t path2_len = strlen (path2);
  apr_size_t min_len = ((path1_len < path2_len) ? path1_len : path2_len);
  apr_size_t i = 0;

  assert (is_canonical (path1, path1_len));
  assert (is_canonical (path2, path2_len));

  /* Skip past common prefix. */
  while (i < min_len && path1[i] == path2[i])
    ++i;

  /* Are the paths exactly the same? */
  if ((path1_len == path2_len) && (i >= min_len))
    return 0;    

  /* Children of paths are greater than their parents, but less than
     greater siblings of their parents. */
  if ((path1[i] == SVN_UTF8_FSLASH) && (path2[i] == 0))
    return 1;
  if ((path2[i] == SVN_UTF8_FSLASH) && (path1[i] == 0))
    return -1;
  if (path1[i] == SVN_UTF8_FSLASH)
    return -1;
  if (path2[i] == SVN_UTF8_FSLASH)
    return 1;

  /* Common prefix was skipped above, next character is compared to
     determine order */
  return path1[i] < path2[i] ? -1 : 1;
}


/* Return the string length of the longest common ancestor of PATH1 and PATH2.  
 *
 * This function handles everything except the URL-handling logic 
 * of svn_path_get_longest_ancestor, and assumes that PATH1 and 
 * PATH2 are *not* URLs.  
 *
 * If the two paths do not share a common ancestor, return 0. 
 *
 * New strings are allocated in POOL.
 */
static apr_size_t
get_path_ancestor_length (const char *path1,
                          const char *path2,
                          apr_pool_t *pool)
{
  apr_size_t path1_len, path2_len;
  apr_size_t i = 0;
  apr_size_t last_dirsep = 0;
  
  path1_len = strlen (path1);
  path2_len = strlen (path2);

  if (SVN_PATH_IS_EMPTY (path1) || SVN_PATH_IS_EMPTY (path2))
    return 0;

  while (path1[i] == path2[i])
    {
      /* Keep track of the last directory separator we hit. */
      if (path1[i] == SVN_UTF8_FSLASH)
        last_dirsep = i;

      i++;

      /* If we get to the end of either path, break out. */
      if ((i == path1_len) || (i == path2_len))
        break;
    }

  /* last_dirsep is now the offset of the last directory separator we
     crossed before reaching a non-matching byte.  i is the offset of
     that non-matching byte. */
  if (((i == path1_len) && (path2[i] == SVN_UTF8_FSLASH))
           || ((i == path2_len) && (path1[i] == SVN_UTF8_FSLASH))
           || ((i == path1_len) && (i == path2_len)))
    return i;
  else
    return last_dirsep;
}


char *
svn_path_get_longest_ancestor (const char *path1,
                               const char *path2,
                               apr_pool_t *pool)
{
  svn_boolean_t path1_is_url, path2_is_url;
  path1_is_url = svn_path_is_url (path1);
  path2_is_url = svn_path_is_url (path2);

  if (path1_is_url && path2_is_url) 
    {
      apr_size_t path_ancestor_len; 
      apr_size_t i = 0;

      /* Find ':' */
      while (1)
        {
          /* No shared protocol => no common prefix */
          if (path1[i] != path2[i])
            return apr_pmemdup (pool, SVN_EMPTY_PATH, 
                                sizeof (SVN_EMPTY_PATH));

          if (path1[i] == SVN_UTF8_COLON) 
            break;

          /* They're both URLs, so EOS can't come before ':' */
          assert ((path1[i] != '\0') && (path2[i] != '\0'));

          i++;
        }

      i += 3;  /* Advance past '://' */

      path_ancestor_len = get_path_ancestor_length (path1 + i, path2 + i, 
                                                    pool);

      if (path_ancestor_len == 0)
        return apr_pmemdup (pool, SVN_EMPTY_PATH, sizeof (SVN_EMPTY_PATH));
      else
        return apr_pstrndup (pool, path1, path_ancestor_len + i); 
    }

  else if ((! path1_is_url) && (! path2_is_url))
    { 
      return apr_pstrndup (pool, path1, 
                           get_path_ancestor_length (path1, path2, pool));
    }

  else
    {
      /* A URL and a non-URL => no common prefix */
      return apr_pmemdup (pool, SVN_EMPTY_PATH, sizeof (SVN_EMPTY_PATH));
    }
}


const char *
svn_path_is_child (const char *path1,
                   const char *path2,
                   apr_pool_t *pool)
{
  apr_size_t i;

  /* assert (is_canonical (path1, strlen (path1)));  ### Expensive strlen */
  /* assert (is_canonical (path2, strlen (path2)));  ### Expensive strlen */

  /* Allow "" and "foo" to be parent/child */
  if (SVN_PATH_IS_EMPTY (path1))               /* "" is the parent  */
    {
      if (SVN_PATH_IS_EMPTY (path2)            /* "" not a child    */
          || path2[0] == SVN_UTF8_FSLASH)      /* "/foo" not a child */
        return NULL;
      else
        return apr_pstrdup (pool, path2);      /* everything else is child */
    }

  /* Reach the end of at least one of the paths.  How should we handle
     things like path1:"foo///bar" and path2:"foo/bar/baz"?  It doesn't
     appear to arise in the current Subversion code, it's not clear to me
     if they should be parent/child or not. */
  for (i = 0; path1[i] && path2[i]; i++)
    if (path1[i] != path2[i])
      return NULL;

  /* There are two cases that are parent/child
          ...      path1[i] == '\0'
          .../foo  path2[i] == '/'
      or
          /        path1[i] == '\0'
          /foo     path2[i] != '/'
  */
  if (path1[i] == '\0' && path2[i])
    {
      if (path2[i] == SVN_UTF8_FSLASH)
        return apr_pstrdup (pool, path2 + i + 1);
      else if (i == 1 && path1[0] == SVN_UTF8_FSLASH)
        return apr_pstrdup (pool, path2 + 1);
    }

  /* Otherwise, path2 isn't a child. */
  return NULL;
}

svn_boolean_t
svn_path_is_ancestor (const char *path1, const char *path2)
{
  apr_size_t path1_len = strlen (path1);

  /* If path1 is empty and path2 is not absoulte, then path1 is an ancestor. */
  if (SVN_PATH_IS_EMPTY (path1))
    return *path2 != SVN_UTF8_FSLASH;

  /* If path1 is a prefix of path2, then:
     - If path1 ends in a path separator,
     - If the paths are of the same length
     OR
     - path2 starts a new path component after the common prefix,
     then path1 is an ancestor. */
  if (strncmp (path1, path2, path1_len) == 0)
    return path1[path1_len - 1] == SVN_UTF8_FSLASH
      || (path2[path1_len] == SVN_UTF8_FSLASH || path2[path1_len] == '\0');

  return FALSE;
}
apr_array_header_t *
svn_path_decompose (const char *path,
                    apr_pool_t *pool)
{
  apr_size_t i, oldi;

  apr_array_header_t *components = 
    apr_array_make (pool, 1, sizeof(const char *));

  /* assert (is_canonical (path, strlen (path)));  ### Expensive strlen */

  if (SVN_PATH_IS_EMPTY (path))
    return components;  /* ### Should we return a "" component? */

  /* If PATH is absolute, store the '/' as the first component. */
  i = oldi = 0;
  if (path[i] == SVN_UTF8_FSLASH)
    {
      char dirsep = SVN_UTF8_FSLASH;

      *((const char **) apr_array_push (components))
        = apr_pstrmemdup (pool, &dirsep, sizeof (dirsep));

      i++;
      oldi++;
      if (path[i] == '\0') /* path is a single '/' */
        return components;
    }

  do
    {
      if ((path[i] == SVN_UTF8_FSLASH) || (path[i] == '\0'))
        {
          if (SVN_PATH_IS_PLATFORM_EMPTY (path + oldi, i - oldi))
            *((const char **) apr_array_push (components)) = SVN_EMPTY_PATH;
          else
            *((const char **) apr_array_push (components))
              = apr_pstrmemdup (pool, path + oldi, i - oldi);

          i++;
          oldi = i;  /* skipping past the dirsep */
          continue;
        }
      i++;
    }
  while (path[i-1]);

  return components;
}


svn_boolean_t
svn_path_is_single_path_component (const char *name)
{
  /* assert (is_canonical (name, strlen (name)));  ### Expensive strlen */

  /* Can't be empty or `..'  */
  if (SVN_PATH_IS_EMPTY (name)
      || (name[0] == SVN_UTF8_DOT && 
          name[1] == SVN_UTF8_DOT && 
          name[2] == '\0'))
    return FALSE;

  /* Slashes are bad, m'kay... */
  if (strchr (name, SVN_UTF8_FSLASH) != NULL)
    return FALSE;

  /* It is valid.  */
  return TRUE;
}


svn_boolean_t
svn_path_is_backpath_present (const char *path)
{
  int len = strlen (path);
  
  if (! strcmp (path, "\x2E\x2E")) /* ".." */
    return TRUE;

  if (! strncmp (path, "\x2E\x2E\x2F", 3))  /* "../" */
    return TRUE;
  
  if (strstr (path, "\x2F\x2E\x2E\x2F") != NULL) /* "/../" */
    return TRUE;

  if (len >= 3
      && (! strncmp (path + len - 3, "\x2F\x2E\x2E", 3)))  /* "/.." */
    return TRUE;

  return FALSE;
}


/*** URI Stuff ***/

/* Examine PATH as a potential URI, and return a substring of PATH
   that immediately follows the (scheme):// portion of the URI, or
   NULL if PATH doesn't appear to be a valid URI.  The returned value
   is not alloced -- it shares memory with PATH. */
static const char *
skip_uri_scheme (const char *path)
{
  apr_size_t j;

  for (j = 0; path[j]; ++j)
    if (path[j] == SVN_UTF8_COLON || path[j] == SVN_UTF8_FSLASH)
       break;

  if (j > 0 && path[j] == SVN_UTF8_COLON
            && path[j+1] == SVN_UTF8_FSLASH
            && path[j+2] == SVN_UTF8_FSLASH)
    return path + j + 3;

  return NULL;
}

svn_boolean_t 
svn_path_is_url (const char *path)
{
  /* ### This function is reaaaaaaaaaaaaaally stupid right now.
     We're just going to look for:
 
        (scheme)://(optional_stuff)

     Where (scheme) has no ':' or '/' characters.

     Someday it might be nice to have an actual URI parser here.
  */
  return skip_uri_scheme (path) ? TRUE : FALSE;
}



/* Here is the BNF for path components in a URI. "pchar" is a
   character in a path component.

      pchar       = unreserved | escaped | 
                    ":" | "@" | "&" | "=" | "+" | "$" | ","
      unreserved  = alphanum | mark
      mark        = "-" | "_" | "." | "!" | "~" | "*" | "'" | "(" | ")"

   Note that "escaped" doesn't really apply to what users can put in
   their paths, so that really means the set of characters is:

      alphanum | mark | ":" | "@" | "&" | "=" | "+" | "$" | "," 
*/
static const char uri_char_validity[256] = {
  0, 0, 0, 0, 0, 0, 0, 0,   0, 0, 0, 0, 0, 0, 0, 0,
  0, 0, 0, 0, 0, 0, 0, 0,   0, 0, 0, 0, 0, 0, 0, 0,
  0, 1, 0, 0, 1, 0, 1, 1,   1, 1, 1, 1, 1, 1, 1, 1,
  1, 1, 1, 1, 1, 1, 1, 1,   1, 1, 1, 0, 0, 1, 0, 0,

  /* 64 */
  1, 1, 1, 1, 1, 1, 1, 1,   1, 1, 1, 1, 1, 1, 1, 1,
  1, 1, 1, 1, 1, 1, 1, 1,   1, 1, 1, 0, 0, 0, 0, 1,
  0, 1, 1, 1, 1, 1, 1, 1,   1, 1, 1, 1, 1, 1, 1, 1,
  1, 1, 1, 1, 1, 1, 1, 1,   1, 1, 1, 0, 0, 0, 1, 0,

  /* 128 */
  0, 0, 0, 0, 0, 0, 0, 0,   0, 0, 0, 0, 0, 0, 0, 0,
  0, 0, 0, 0, 0, 0, 0, 0,   0, 0, 0, 0, 0, 0, 0, 0,
  0, 0, 0, 0, 0, 0, 0, 0,   0, 0, 0, 0, 0, 0, 0, 0,
  0, 0, 0, 0, 0, 0, 0, 0,   0, 0, 0, 0, 0, 0, 0, 0,

  /* 192 */
  0, 0, 0, 0, 0, 0, 0, 0,   0, 0, 0, 0, 0, 0, 0, 0,
  0, 0, 0, 0, 0, 0, 0, 0,   0, 0, 0, 0, 0, 0, 0, 0,
  0, 0, 0, 0, 0, 0, 0, 0,   0, 0, 0, 0, 0, 0, 0, 0,
  0, 0, 0, 0, 0, 0, 0, 0,   0, 0, 0, 0, 0, 0, 0, 0,
};

#if APR_CHARSET_EBCDIC
/* ebcdic version of uri validity table */
static const char uri_char_validity_native[256] = {
  0, 0, 0, 0, 0, 0, 0, 0,   0, 0, 0, 0, 0, 0, 0, 0,
  0, 0, 0, 0, 0, 0, 0, 0,   0, 0, 0, 0, 0, 0, 0, 0,
  0, 0, 0, 0, 0, 0, 0, 0,   0, 0, 0, 0, 0, 0, 0, 0,
  0, 0, 0, 0, 0, 0, 0, 0,   0, 0, 0, 0, 0, 0, 0, 0,
  
  /* 64 */
  0, 0, 0, 0, 0, 0, 0, 0,   0, 0, 0, 1, 0, 1, 1, 0,
  1, 0, 0, 0, 0, 0, 0, 0,   0, 0, 1, 1, 1, 1, 0, 0,
  1, 1, 0, 0, 0, 0, 0, 0,   0, 0, 0, 1, 0, 1, 0, 0,
  0, 0, 0, 0, 0, 0, 0, 0,   0, 0, 1, 0, 1, 1, 1, 0,
  
  /* 128 */
  0, 1, 1, 1, 1, 1, 1, 1,   1, 1, 0, 0, 0, 0, 0, 0,
  0, 1, 1, 1, 1, 1, 1, 1,   1, 1, 0, 0, 0, 0, 0, 0,
  0, 1, 1, 1, 1, 1, 1, 1,   1, 1, 0, 0, 0, 0, 0, 0,
  0, 0, 0, 0, 0, 0, 0, 0,   0, 0, 0, 0, 0, 0, 0, 0,
  
  /* 192 */
  0, 1, 1, 1, 1, 1, 1, 1,   1, 1, 0, 0, 0, 0, 0, 0,
  0, 1, 1, 1, 1, 1, 1, 1,   1, 1, 0, 0, 0, 0, 0, 0,
  0, 0, 1, 1, 1, 1, 1, 1,   1, 1, 0, 0, 0, 0, 0, 0,
  1, 1, 1, 1, 1, 1, 1, 1,   1, 1, 0, 0, 0, 0, 0, 0
};
#endif


svn_boolean_t 
svn_path_is_uri_safe (const char *path)
{
  apr_size_t i;

  /* Skip the URI scheme. */
  path = skip_uri_scheme (path);

  /* No scheme?  Get outta here. */
  if (! path)
    return FALSE;

  /* Skip to the first slash that's after the URI scheme. */
  path = strchr (path, SVN_UTF8_FSLASH);

  /* If there's no first slash, then there's only a host portion;
     therefore there couldn't be any uri-unsafe characters after the
     host... so return true. */
  if (path == NULL)
    return TRUE;

  for (i = 0; path[i]; i++)
    {
      /* Allow '%XX' (where each X is a hex digit) */
      if (path[i] == SVN_UTF8_PERCENT)
        {
          if (APR_IS_ASCII_XDIGIT (path[i + 1]) && 
              APR_IS_ASCII_XDIGIT (path[i + 2]))
            {
              i += 2;
              continue;
            }
          return FALSE;
        }
      else if (! uri_char_validity[((unsigned char)path[i])])
        {
          return FALSE;
        }
    } 

  return TRUE;
}
  
/* URI-encode each character c in PATH for which TABLE[c] is 0.
   If no encoding was needed, return PATH, else return a new string allocated
   in POOL. */
static const char *
uri_escape (const char *path, const char table[], svn_boolean_t escape_ibm,
            svn_boolean_t use_ebcdic_escapes, apr_pool_t *pool)
{
  svn_stringbuf_t *retstr;
  apr_size_t i, copied = 0;
  int c;
  
  /* If we know which validity table was passed we can deduce the encoding of
   * path:
   * 
   * 1) uri_char_validity        --> utf-8  --> utf8_path = TRUE
   * 2) iri_escape_chars         --> utf-8  --> utf8_path = TRUE
   * 3) uri_autoescape_chars     --> utf-8  --> utf8_path = TRUE
   * 4) uri_char_validity_native --> ebcdic --> utf8_path = FALSE
   * 
   * So we just pick something unique about the uri_char_validity_native table:
   * 
   * e.g. uri_char_validity_native[128] ^ uri_char_validity_native[129] == 1
   *      only for that table, so that's the test!  
   * 
   * See something you like better? Go nuts!
   */
  svn_boolean_t utf8_path = table[128] ^ table[129] ? FALSE : TRUE;

  retstr = svn_stringbuf_create ("", pool);
  for (i = 0; path[i]; i++)
    {
      c = (unsigned char)path[i];
      if (table[c])
        continue;
      
#if APR_CHARSET_EBCDIC
      /* We don't want to escape IBM's utf-8 transforms in an ebcdic path if 
       * escape_ibm flag is false.  To detect a transform, the char is escaped
       * to it's utf-8 value with ap_escape_path_segment(), the first hex 
       * escape digit is converted to an int, and if that int is greater
       * than 7 the escaped byte pattern starts with a '1' and that char is 
       * part of a utf-8 transform. 
       */
      if (!utf8_path &&
          !escape_ibm)
        {
          char *escaped_str = ap_escape_path_segment(pool, &(path[i]));	
          apr_int64_t hex_digit_val;
          escaped_str[2] = '\0';
          hex_digit_val = apr_strtoi64(&(escaped_str[1]), NULL, 16);
          if (hex_digit_val  > 7 )
            continue;
        }
#endif

      /* If we got here, we're looking at a character that isn't
         supported by the (or at least, our) URI encoding scheme.  We
         need to escape this character.  */

      /* First things first, copy all the good stuff that we haven't
         yet copied into our output buffer. */
      if (i - copied)
        svn_stringbuf_appendbytes (retstr, path + copied, 
                                   i - copied);
      
      /* Now, sprintf() in our escaped character, making sure our
         buffer is big enough to hold the '%' and two digits.  We cast
         the C to unsigned char here because the 'X' format character
         will be tempted to treat it as an unsigned int...which causes
         problem when messing with 0x80-0xFF chars.  We also need space
         for a null as sprintf will write one. */
      svn_stringbuf_ensure (retstr, retstr->len + 4);
      if(utf8_path)
        {
#if !APR_CHARSET_EBCDIC
          sprintf (retstr->data + retstr->len, "%%%02X", (unsigned char)c);
#else
          /* sprintf returns ebcdic characters, which we obviously don't want
           * when working on a utf-8 path.  So use apr_psprintf() to obtain
           * a temporary ebcdic string, convert it to utf-8, and append to
           * retstr. */
          int d;
          /* Get ebcdic escape string. */
          char *str = apr_psprintf (pool, "%%%02X", (unsigned char)c);
          /* */
          retstr->data[retstr->len] = SVN_UTF8_PERCENT;
          for (d = 1; d < 3; d++)
            {
              /* We are always dealing with ebcdic string representations of
               * hex digits, so we can convert without calling
               * svn_utf_cstring_to_utf8.  
               * 
               * Checking for upper and lower case may not be necessary if
               * apr_psprintf() is guaranteed to return a consistent
               * case (?) */
              if (str[d] >= '0' && str[d] <= '9')
                retstr->data[retstr->len + d] = (str[d] - 192);
              else if (str[d] >= 'a' && str[d] <= 'f')
                retstr->data[retstr->len + d] = (str[d] - 32);
              else /* c == {A | B | C | D | E | F} */
              retstr->data[retstr->len + d] = (str[d] - 128);
            }
          /* null-terminate the buffer */
          retstr->data[retstr->len + 3] = '\0';
#endif
          retstr->len += 3;
        } 
#if APR_CHARSET_EBCDIC
      else if(!use_ebcdic_escapes)
        { 
        /** This is an ebcdic path.  ap_escape_path_segment() on IBM Apache
         *  conveniently escapes with the utf-8 value for the escaped
         *  character, how nice!  But more importantly it also handles utf-8
         *  values transformed into ebcdic, and does so based on the netCCSID
         *  and fsCCSID Apache is using. 
         */
           svn_stringbuf_appendbytes (retstr,
                                     ap_escape_path_segment(pool, &(path[i])),
                                     3);
        }
      else /* use_ebcdic_escapes */
        {
        /** This is an ebcdic path and the caller wants the escapes to use
         *  ebcdic equivalents.  e.g. "Dir A" --> "Dir%40A" 
         */
          sprintf (retstr->data + retstr->len, "%%%02X", (unsigned char)c);
          retstr->len += 3;
          retstr->data[retstr->len + 3] = '\0';
        }
#endif
      /* Finally, update our copy counter. */
      copied = i + 1;
    }

  /* If we didn't encode anything, we don't need to duplicate the string. */
  if (retstr->len == 0)
    return path;

  /* Anything left to copy? */
  if (i - copied)
    svn_stringbuf_appendbytes (retstr, path + copied, i - copied);

  /* retstr is null-terminated either by sprintf or the svn_stringbuf
     functions. */

  return retstr->data;
}

const char *
svn_path_uri_encode (const char *path, apr_pool_t *pool)
{
  const char *ret;

  ret = uri_escape (path, uri_char_validity, FALSE, FALSE, pool);

  /* Our interface guarantees a copy. */
  if (ret == path)
    return apr_pstrdup (pool, path);
  else
    return ret;
}


#if APR_CHARSET_EBCDIC
const char *
svn_path_uri_encode_native_partial (const char *path, apr_pool_t *pool)
{
  const char *ret;
  ret = uri_escape (path, uri_char_validity_native, FALSE, FALSE, pool);

  /* Our interface guarantees a copy. */
  if (ret == path)
    return apr_pstrdup (pool, path);
  else
    return ret;
}

const char *
svn_path_uri_encode_native_partial_2 (const char *path, apr_pool_t *pool)
{
  const char *ret;
  ret = uri_escape (path, uri_char_validity_native, FALSE, TRUE, pool);

  /* Our interface guarantees a copy. */
  if (ret == path)
    return apr_pstrdup (pool, path);
  else
    return ret;
}

const char *
svn_path_uri_encode_native_full (const char *path, apr_pool_t *pool)
{
  const char *ret;
  ret = uri_escape (path, uri_char_validity_native, TRUE, FALSE, pool);
  	
  /* Our interface guarantees a copy. */
  if (ret == path)
    return apr_pstrdup (pool, path);
  else
    return ret;
}
#endif


static const char iri_escape_chars[256] = {
  1, 1, 1, 1, 1, 1, 1, 1,  1, 1, 1, 1, 1, 1, 1, 1,
  1, 1, 1, 1, 1, 1, 1, 1,  1, 1, 1, 1, 1, 1, 1, 1,
  1, 1, 1, 1, 1, 1, 1, 1,  1, 1, 1, 1, 1, 1, 1, 1,
  1, 1, 1, 1, 1, 1, 1, 1,  1, 1, 1, 1, 1, 1, 1, 1,
  1, 1, 1, 1, 1, 1, 1, 1,  1, 1, 1, 1, 1, 1, 1, 1,
  1, 1, 1, 1, 1, 1, 1, 1,  1, 1, 1, 1, 1, 1, 1, 1,
  1, 1, 1, 1, 1, 1, 1, 1,  1, 1, 1, 1, 1, 1, 1, 1,
  1, 1, 1, 1, 1, 1, 1, 1,  1, 1, 1, 1, 1, 1, 1, 1,

  /* 128 */
  0, 0, 0, 0, 0, 0, 0, 0,  0, 0, 0, 0, 0, 0, 0, 0,
  0, 0, 0, 0, 0, 0, 0, 0,  0, 0, 0, 0, 0, 0, 0, 0,
  0, 0, 0, 0, 0, 0, 0, 0,  0, 0, 0, 0, 0, 0, 0, 0,
  0, 0, 0, 0, 0, 0, 0, 0,  0, 0, 0, 0, 0, 0, 0, 0,
  0, 0, 0, 0, 0, 0, 0, 0,  0, 0, 0, 0, 0, 0, 0, 0,
  0, 0, 0, 0, 0, 0, 0, 0,  0, 0, 0, 0, 0, 0, 0, 0,
  0, 0, 0, 0, 0, 0, 0, 0,  0, 0, 0, 0, 0, 0, 0, 0,
  0, 0, 0, 0, 0, 0, 0, 0,  0, 0, 0, 0, 0, 0, 0, 0
};

const char *
svn_path_uri_from_iri (const char *iri, apr_pool_t *pool)
{
  return uri_escape (iri, iri_escape_chars, FALSE, FALSE, pool);
}

const char uri_autoescape_chars[256] = {
  1, 1, 1, 1, 1, 1, 1, 1,  1, 1, 1, 1, 1, 1, 1, 1,
  1, 1, 1, 1, 1, 1, 1, 1,  1, 1, 1, 1, 1, 1, 1, 1,
  0, 1, 0, 1, 1, 1, 1, 1,  1, 1, 1, 1, 1, 1, 1, 1,
  1, 1, 1, 1, 1, 1, 1, 1,  1, 1, 1, 1, 0, 1, 0, 1,

  /* 64 */
  1, 1, 1, 1, 1, 1, 1, 1,  1, 1, 1, 1, 1, 1, 1, 1,
  1, 1, 1, 1, 1, 1, 1, 1,  1, 1, 1, 1, 0, 1, 0, 1,
  0, 1, 1, 1, 1, 1, 1, 1,  1, 1, 1, 1, 1, 1, 1, 1,
  1, 1, 1, 1, 1, 1, 1, 1,  1, 1, 1, 0, 0, 0, 1, 1,

  /* 128 */
  1, 1, 1, 1, 1, 1, 1, 1,  1, 1, 1, 1, 1, 1, 1, 1,
  1, 1, 1, 1, 1, 1, 1, 1,  1, 1, 1, 1, 1, 1, 1, 1,
  1, 1, 1, 1, 1, 1, 1, 1,  1, 1, 1, 1, 1, 1, 1, 1,
  1, 1, 1, 1, 1, 1, 1, 1,  1, 1, 1, 1, 1, 1, 1, 1,

  /* 192 */
  1, 1, 1, 1, 1, 1, 1, 1,  1, 1, 1, 1, 1, 1, 1, 1,
  1, 1, 1, 1, 1, 1, 1, 1,  1, 1, 1, 1, 1, 1, 1, 1,
  1, 1, 1, 1, 1, 1, 1, 1,  1, 1, 1, 1, 1, 1, 1, 1,
  1, 1, 1, 1, 1, 1, 1, 1,  1, 1, 1, 1, 1, 1, 1, 1,
};

const char *
svn_path_uri_autoescape (const char *uri, apr_pool_t *pool)
{
  return uri_escape (uri, uri_autoescape_chars, FALSE, FALSE, pool);
}

const char *
svn_path_uri_decode (const char *path, apr_pool_t *pool)
{
  svn_stringbuf_t *retstr;
  apr_size_t i;
  svn_boolean_t query_start = FALSE;

  retstr = svn_stringbuf_create ("", pool);

  /* avoid repeated realloc */
  svn_stringbuf_ensure (retstr, strlen (path) + 1); 

  retstr->len = 0;
  for (i = 0; path[i]; i++)
    {
      char c = path[i];

      if (c == SVN_UTF8_QUESTION)
        {
          /* Mark the start of the query string, if it exists. */
          query_start = TRUE;
        }
      else if (c == SVN_UTF8_PLUS && query_start)
        {
          /* Only do this if we are into the query string.
           * RFC 2396, section 3.3  */
          c = SVN_UTF8_SPACE;
        }
      else if (c == SVN_UTF8_PERCENT && APR_IS_ASCII_XDIGIT (path[i + 1])
               && APR_IS_ASCII_XDIGIT (path[i+2]))
        {
#if APR_CHARSET_EBCDIC
          int d;
#endif
          char digitz[3];
          digitz[0] = path[++i];
          digitz[1] = path[++i];
          digitz[2] = '\0';
#if APR_CHARSET_EBCDIC
          /* strol wants ebcdic encoded numbers on ebcdic platforms.
           * Since we can only get to this point if dealing with xdigits we can
           * make some assumptions and do a quick utf8 --> ebcdic conversion. */
          for (d = 0; d < 2; d++)
          {
            if (digitz[d] >= SVN_UTF8_0 && digitz[d] <= SVN_UTF8_9)
              digitz[d] += 192;
            else if (digitz[d] >= SVN_UTF8_a && digitz[d] <= SVN_UTF8_f)
              digitz[d] += 32;
            else /* digitz[d] == {A | B | C | D | E | F} */
              digitz[d] += 128;
          }
#endif
          c = (char)(strtol (digitz, NULL, 16));
        }

      retstr->data[retstr->len++] = c;
    }

  /* Null-terminate this bad-boy. */
  retstr->data[retstr->len] = 0;

  return retstr->data;
}

#if APR_CHARSET_EBCDIC
const char *
svn_path_uri_decode_native (const char *path, apr_pool_t *pool)
{
  if(svn_utf_cstring_to_netccsid(&path, path, pool))
    return path;
  path = svn_path_uri_decode (path, pool);
  svn_utf_cstring_from_netccsid(&path, path, pool);
  return path;
}
#endif

const char *
svn_path_url_add_component (const char *url,
                            const char *component,
                            apr_pool_t *pool)
{
  /* URL can have trailing '/' */
  url = svn_path_canonicalize (url, pool);

  return svn_path_join (url, svn_path_uri_encode (component, pool), pool);
}

svn_error_t *
svn_path_get_absolute(const char **pabsolute,
                      const char *relative,
                      apr_pool_t *pool)
{
  /* We call svn_path_canonicalize() on the input data, rather
     than the output, so that `buffer' can be returned directly
     without const vs non-const issues. */
  /* ### This comment seems totally wrong, what? --xbc */

  char *buffer;
  apr_status_t apr_err;
  const char *path_apr;

  relative = svn_path_canonicalize (relative, pool);

  if (svn_path_is_url (relative))
    {
      *pabsolute = apr_pstrdup (pool, relative);
    }
  else
    {
      SVN_ERR (svn_path_cstring_from_utf8 (&path_apr, relative, pool));
      apr_err = apr_filepath_merge(&buffer, NULL,
                                   path_apr,
                                   (APR_FILEPATH_NOTRELATIVE
                                    | APR_FILEPATH_TRUENAME),
                                   pool);

      if (apr_err)
        return svn_error_createf(SVN_ERR_BAD_FILENAME, NULL,
                                 _("Couldn't determine absolute path of '%s'"), 
                                 svn_path_local_style (relative, pool));
      SVN_ERR (svn_path_cstring_to_utf8 (pabsolute, buffer, pool));
      *pabsolute = svn_path_canonicalize (*pabsolute, pool);
    }

  return SVN_NO_ERROR;
}


svn_error_t *
svn_path_split_if_file(const char *path,
                       const char **pdirectory,
                       const char **pfile,
                       apr_pool_t *pool)
{
  apr_finfo_t finfo;
  svn_error_t *err;

  /* assert (is_canonical (path, strlen (path)));  ### Expensive strlen */

  err = svn_io_stat(&finfo, path, APR_FINFO_TYPE, pool);
  if (err && ! APR_STATUS_IS_ENOENT(err->apr_err))
    return err;

  if (err || finfo.filetype == APR_REG)
    {
      if (err)
        svn_error_clear (err);
      svn_path_split(path, pdirectory, pfile, pool);
    }
  else if (finfo.filetype == APR_DIR)
    {
      *pdirectory = path;
      *pfile = SVN_EMPTY_PATH;
    }
  else 
    {
      return svn_error_createf(SVN_ERR_BAD_FILENAME, NULL,
                               _("'%s' is neither a file nor a directory name"),
                               svn_path_local_style(path, pool));
    }

  return SVN_NO_ERROR;
}


const char *
svn_path_canonicalize (const char *path, apr_pool_t *pool)
{
  char *canon, *dst;
  const char *src;
  apr_size_t seglen;
  apr_size_t canon_segments = 0;
  svn_boolean_t uri;

  dst = canon = apr_pcalloc (pool, strlen (path) + 1);

  /* Copy over the URI scheme if present. */
  src = skip_uri_scheme (path);
  if (src)
    {
      uri = TRUE;
      memcpy (dst, path, src - path);
      dst += (src - path);
    }
  else
    {
      uri = FALSE;
      src = path;
    }

  /* If this is an absolute path, then just copy over the initial
     separator character. */
  if (*src == SVN_UTF8_FSLASH)
    {
      *(dst++) = *(src++);

#if defined(WIN32) || defined(__CYGWIN__)
      /* On Windows permit two leading separator characters which means an
       * UNC path.  However, a double slash in a URI after the scheme is never
       * valid. */
      if (!uri && *src == SVN_UTF8_FSLASH)
        *(dst++) = *(src++);
#endif /* WIN32 or Cygwin */
      
    }

  while (*src)
    {
      /* Parse each segment, find the closing '/' */
      const char *next = src;
      while (*next && (*next != SVN_UTF8_FSLASH))
        ++next;

      seglen = next - src;

      if (seglen == 0 || (seglen == 1 && src[0] == SVN_UTF8_DOT))
        {
          /* Noop segment, so do nothing. */
        }
      else
        {
          /* An actual segment, append it to the destination path */
          if (*next)
            seglen++;
          memcpy (dst, src, seglen);
          dst += seglen;
          canon_segments++;
        }

      /* Skip over trailing slash to the next segment. */
      src = next;
      if (*src)
        src++;
    }

  /* Remove the trailing slash. */
  if ((canon_segments > 0 || uri) && *(dst - 1) == SVN_UTF8_FSLASH)
    dst--;
  
  *dst = '\0';

#if defined(WIN32) || defined(__CYGWIN__)
  /* Skip leading double slashes when there are less than 2
   * canon segments. UNC paths *MUST* have two segments. */
  if (canon_segments < 2 && canon[0] == '/' && canon[1] == '/')
    return canon + 1;
#endif /* WIN32 or Cygwin */

  return canon;
}



/** Get APR's internal path encoding. */
static svn_error_t *
get_path_encoding (svn_boolean_t *path_is_utf8, apr_pool_t *pool)
{
  apr_status_t apr_err;
  int encoding_style;

  apr_err = apr_filepath_encoding (&encoding_style, pool);
  if (apr_err)
    return svn_error_wrap_apr (apr_err,
                               _("Can't determine the native path encoding"));

  /* ### What to do about APR_FILEPATH_ENCODING_UNKNOWN?
     Well, for now we'll just punt to the svn_utf_ functions;
     those will at least do the ASCII-subset check. */
  *path_is_utf8 = (encoding_style == APR_FILEPATH_ENCODING_UTF8);

  return SVN_NO_ERROR;
}


svn_error_t *
svn_path_cstring_from_utf8 (const char **path_apr,
                            const char *path_utf8,
                            apr_pool_t *pool)
{
  svn_boolean_t path_is_utf8;
  SVN_ERR (get_path_encoding (&path_is_utf8, pool));
  if (path_is_utf8)
    {
      *path_apr = apr_pstrdup (pool, path_utf8);
      return SVN_NO_ERROR;
    }
  else
    return svn_utf_cstring_from_utf8 (path_apr, path_utf8, pool);
}


svn_error_t *
svn_path_cstring_to_utf8 (const char **path_utf8,
                          const char *path_apr,
                          apr_pool_t *pool)
{
  svn_boolean_t path_is_utf8;
  SVN_ERR (get_path_encoding (&path_is_utf8, pool));
  if (path_is_utf8)
    {
      *path_utf8 = apr_pstrdup (pool, path_apr);
      return SVN_NO_ERROR;
    }
  else
    return svn_utf_cstring_to_utf8 (path_utf8, path_apr, pool);
}

svn_error_t *
svn_path_check_valid (const char *path, apr_pool_t *pool)
{
  const char *c;

  for (c = path; *c; c++)
    {
      if (svn_ctype_iscntrl(*c))
        {
          return svn_error_createf (
            SVN_ERR_FS_PATH_SYNTAX, NULL,
            _("Invalid control character '0x%02x' in path '%s'"),
            *c,
            svn_path_local_style (path, pool));
        }
    }

  return SVN_NO_ERROR;
}
