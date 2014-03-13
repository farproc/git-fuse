/*
 * git-fuse - a simple fuse 'driver' to allow you to mount a git repo and
 * browse the branches/tags etc like directories. (all read only)
 *
 * Tim Bateman <tim.bateman@gmail.com>
 *
 * build like so:
 *
 * gcc `pkg-config osxfuse,libgit2 --cflags --libs` -Wall git-fuse.c -o git-fuse
 *
 * ./git-fuse /tmp/mnt /Users/tim/devel/local/linux.git
 *
 */


#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <stdio.h>

#define FUSE_USE_VERSION  26
#include <fuse.h>

#include <git2.h>

static git_repository   *g_pRepo          = NULL;
const char              *g_pszBranchPath  = "refs/heads/";

static char*
gitfuse_getRef( const char *pszPath )
{
  char *pszRetVal = NULL;
  char *pszEnd    = NULL;
  unsigned int nLen = 0;
  // format is:
  // /<branch-name>/<path/to/file/in/branch>

  // just a basic check first
  if( pszPath && pszPath[0] == '/' )
  {
    pszEnd = strstr( pszPath + 1, "/" );

    if( NULL == pszEnd )
    {
      // if there wasn't a match - stick pszEnd at the very end of the
      // string - implied '/'
      pszEnd = (char*)( pszPath + ( strlen( pszPath ) - 1 ) );
    }

    if( pszEnd )
    {
      nLen = ( pszEnd - pszPath ) /* the initial '/' makes up for our NULL */;
      pszRetVal = calloc( nLen, sizeof( char ) );

      if( pszRetVal )
      {
        memcpy( pszRetVal, pszPath + 1, nLen );

        // make sure we don't have an trailing '/'
        for( nLen = 0; nLen < strlen( pszRetVal ); nLen++ )
        {
          if( '/' == pszRetVal[ nLen ] )
          {
            pszRetVal[ nLen ] = '\0';
          }
        }
      }
    }
  }
  return pszRetVal;
}

static char*
gitfuse_getPath( const char *pszPath )
{
  char *pszRetVal = NULL;
  char *pszStart  = NULL;
  unsigned int nLen = 0;
  // format is:
  // /<branch-name>/<path/to/file/in/branch>

  // just a basic check first
  if( pszPath && pszPath[0] == '/' )
  {
    pszStart = strstr( pszPath + 1, "/" );

    if( pszStart )
    {
      pszStart++; // move over the '/'

      nLen = strlen( pszStart ) + 1 /* NULL */;

      pszRetVal = calloc( nLen, sizeof( char ) );

      if( pszRetVal )
      {
        memcpy( pszRetVal, pszStart, nLen - 1 );
      }
    }
  }
  return pszRetVal;
}

static git_tree*
gitfuse_getRefTree( char *pszReference )
{
  git_tree      *pRetValue   = NULL;
  git_oid        nCommitOid;
  git_commit    *pCommit     = NULL;
  char           pszFullRef[1024] = { 0 };

  if( pszReference )
  {
    strcat( pszFullRef, g_pszBranchPath );
    strcat( pszFullRef, pszReference    );

    if( '/' == pszFullRef[ strlen( pszFullRef ) - 1 ] )
    {
      pszFullRef[ strlen( pszFullRef ) - 1 ] = '\0';
    }

    // get the oid
    if( 0 == git_reference_name_to_id( &nCommitOid, g_pRepo, pszFullRef ) )
    {
      // get the commit object
      if( 0 == git_commit_lookup( &pCommit, g_pRepo, &nCommitOid ) )
      {
        // if we use git_commit_tree it will be freed in during the call
        // to git_commit_free - instead i'll look it up indepentantly
        if( 0 == git_tree_lookup( &pRetValue, g_pRepo, git_commit_tree_id( pCommit ) ) )
        {
          // sweet
        }
        else
        {
          // not so sweet
        }

        git_commit_free( pCommit );
      }
    }
  }

  return pRetValue;
}

static git_tree_entry*
gitfuse_getRefEntry( char *pszReference, char *pszPath )
{
  git_tree        *pRefTree     = NULL;
  git_tree_entry  *pEntry       = NULL;

  pRefTree = gitfuse_getRefTree( pszReference );
  if( pRefTree )
  {
    if( NULL != pszPath )
    {
      ( void ) git_tree_entry_bypath( &pEntry, pRefTree, pszPath );
    }
    else
    {

    }
  }

  return pEntry;
}

static git_tree*
gitfuse_getRefPath( char *pszReference, char *pszPath )
{
  git_tree        *pRefTree     = NULL;
  git_tree_entry  *pEntry       = NULL;

  if( NULL == pszPath || 0 == strcmp( pszPath, "/" ) )
  {
    pRefTree = gitfuse_getRefTree( pszReference );
  }
  else
  {
    pEntry = gitfuse_getRefEntry( pszReference, pszPath );
    if( pEntry )
    {
      ( void ) git_tree_lookup( &pRefTree, g_pRepo, git_tree_entry_id( pEntry ) );

      git_tree_entry_free( pEntry );
    }
  }

  return pRefTree;
}



static bool
gitfuse_isDirectory( char *pszReference, char *pszPath )
{
  bool           fDir         = false;
  git_tree      *pRefTree     = NULL;

  pRefTree = gitfuse_getRefPath( pszReference, pszPath );
  if( pRefTree )
  {
    fDir = true;
  }

  return fDir;
}

static size_t
gitfuse_getFileSize( git_tree_entry *pEntry )
{
  size_t          nRetVal   = 0;
  const git_oid  *pnFileOid;
  git_blob       *pFile     = NULL;

  pnFileOid = git_tree_entry_id( pEntry );

  if( pnFileOid )
  {
    if( 0 == git_blob_lookup( &pFile, g_pRepo, pnFileOid ) )
    {
      nRetVal = git_blob_rawsize( pFile );
    }
  }

  return nRetVal;
}

static const char*
gitfuse_buildBranchDirName( const char *pszFullBranchName )
{
  const char   *pRetVal     = NULL;
  size_t  nIdx        = 0;

  if( pszFullBranchName )
  {
    nIdx = strlen( pszFullBranchName ) - 1 /* zero indexed */;
    do
    {
      if( pszFullBranchName[ nIdx ] == '/' )
      {
        pRetVal = pszFullBranchName + ( nIdx + 1 );
        break;
      }
    }while( nIdx-- );
  }

  return pRetVal;
}


static void
gitfuse_getattr_dir( struct stat *stbuf, unsigned int nLinks )
{
  stbuf->st_mode = S_IFDIR | 0755;
  stbuf->st_nlink= nLinks;
}

static int
gitfuse_getattr(const char *path, struct stat *stbuf)
{
  int     nRetVal   = 0;

  if( NULL == path || NULL == stbuf )
  {
    return -ENOMEM;
  }

  //
  // make sure its 'clean'
  //
  memset(stbuf, 0, sizeof(struct stat));

  // the root of the file-system
  if( 0 == strcmp( path, "/" ) || 0 == strcmp( path, "." ) || 0 == strcmp( path, ".." )  )
  {
    gitfuse_getattr_dir( stbuf, 3 ); // TODO: should be the ref count
  }
  else
  {
    char            *pszRef   = gitfuse_getRef(  path );
    char            *pszPath  = gitfuse_getPath( path );
    git_tree_entry  *pEntry   = NULL;

    if( pszRef && !pszPath )
    {
      // its a reference dir
      gitfuse_getattr_dir( stbuf, 3 );
    }
    else if( gitfuse_isDirectory( pszRef, pszPath ) )
    {
      // its a directory
      gitfuse_getattr_dir( stbuf, 3 );
    }
    else
    {

      pEntry = gitfuse_getRefEntry( pszRef, pszPath );
      if( pEntry )
      {

        switch( git_tree_entry_type( pEntry ) )
        {
          case GIT_OBJ_BLOB:
            stbuf->st_mode = S_IFREG | 0444;
            stbuf->st_nlink= 1;
            stbuf->st_size = gitfuse_getFileSize( pEntry );
            break;
          default:
            // WTF?!!!
            break;
        }

        git_tree_entry_free( pEntry );
      }
      else
      {
        nRetVal = -ENOENT;
      }
    }

    if( pszRef )
    {
      free( pszRef );
    }
    if( pszPath )
    {
      free( pszPath );
    }
  }

  return nRetVal;
}

static int
gitfuse_open(const char *path, struct fuse_file_info *fi)
{
  int              nRetVal  = 0;
  char            *pszRef   = gitfuse_getRef(  path );
  char            *pszPath  = gitfuse_getPath( path );
  git_tree_entry  *pEntry = NULL;

  if ((fi->flags & O_ACCMODE) != O_RDONLY) /* Only reading allowed. */
    nRetVal = -EACCES;

  pEntry = gitfuse_getRefEntry( pszRef, pszPath );
  if( pEntry )
  {
    nRetVal = 0;
    git_tree_entry_free( pEntry );
  }
  else
  {
    nRetVal = -ENOENT;
  }

  if( pszRef )
  {
    free( pszRef );
  }
  if( pszPath )
  {
    free( pszPath );
  }

  return nRetVal;
}

static int
gitfuse_readdir(const char *path, void *buf, fuse_fill_dir_t filler,
              off_t offset, struct fuse_file_info *fi)
{
  int             nRetVal     = 0;
  git_strarray    ref_list;
  const char     *pszRefName  = NULL;
  git_reference  *pReference  = NULL;
  unsigned int    nIdx        = 0;
  char           *pszRef      = NULL;
  char           *pszPath     = NULL;

  git_tree       *pTree       = NULL;
  size_t          nTreeIdx    = 0;

  if( strcmp(path, "/") == 0 )
  {
    //
    // provide a listing of references
    //
    filler(buf, ".", NULL, 0);           /* Current directory (.)  */
    filler(buf, "..", NULL, 0);          /* Parent directory (..)  */

    if( 0 == git_reference_list( &ref_list, g_pRepo ) )
    {
      for( nIdx = 0; nIdx < ref_list.count; nIdx++ )
      {
        pszRefName = ref_list.strings[ nIdx ];

        // see if we're interesting
        if( 0 == git_reference_lookup( &pReference, g_pRepo, pszRefName ) )
        {
          if( git_reference_is_branch( pReference ) )
          {
            filler(buf, gitfuse_buildBranchDirName( pszRefName ), NULL, 0);
          }
          /* CURRENTLY NO TAG SUPPORT
          else if( git_reference_is_tag( pReference ) )
          {
            filler(buf, pszRefName, NULL, 0);
          }
          */
          else
          {
            //
            // ignore
            //
          }

          git_reference_free( pReference );
          pReference = NULL;
        }
        else
        {
          // failed to get reference
        }
      }

      git_strarray_free( &ref_list );
    }
    else
    {
      // failed to get reference listing
    }
    nRetVal = 0;
  }
  else
  {
    filler(buf, ".", NULL, 0);           /* Current directory (.)  */
    filler(buf, "..", NULL, 0);          /* Parent directory (..)  */

    pszRef      = gitfuse_getRef( path );
    pszPath     = gitfuse_getPath( path );

    pTree       = gitfuse_getRefPath( pszRef, pszPath );

    if( pTree )
    {
      for( nTreeIdx = 0; nTreeIdx < git_tree_entrycount( pTree ); nTreeIdx++ )
      {
        const git_tree_entry *pEntry = git_tree_entry_byindex( pTree, nTreeIdx );

        filler(buf, git_tree_entry_name( pEntry ), NULL, 0 );
      }

      nRetVal = 0;
    }
    else
    {
      nRetVal = -ENOENT;
    }
  }

  return nRetVal;
}

static int
gitfuse_read(const char *path, char *buf, size_t size, off_t offset,
           struct fuse_file_info *fi)
{
  int              nRetVal  = 0;
  char            *pszRef   = gitfuse_getRef(  path );
  char            *pszPath  = gitfuse_getPath( path );
  git_tree_entry  *pEntry = NULL;

  const git_oid  *pnFileOid;
  git_blob       *pFile     = NULL;

  pEntry = gitfuse_getRefEntry( pszRef, pszPath );

  if( pEntry )
  {
    if( GIT_OBJ_BLOB == git_tree_entry_type( pEntry ) )
    {
      pnFileOid = git_tree_entry_id( pEntry );

      if( pnFileOid )
      {
        if( 0 == git_blob_lookup( &pFile, g_pRepo, pnFileOid ) )
        {
          if( offset >= git_blob_rawsize( pFile ) )
          {
            // attempting to read past the end of the file */
            nRetVal = 0;
          }
          else if( offset + size > git_blob_rawsize( pFile ) )
          {
            // truncate the read (file smaller than read)
            nRetVal = git_blob_rawsize( pFile ) - offset;
          }
          else
          {
            nRetVal = size;
          }

          if( nRetVal )
          {
            // perform the read
            memcpy(buf, git_blob_rawcontent( pFile ) + offset, nRetVal );
          }
        }
      }
    }
    else
    {
      // no reading directories
      nRetVal = -EACCES;
    }
    git_tree_entry_free( pEntry );
  }
  else
  {
    nRetVal = -ENOENT;
  }

  return nRetVal;
}

static int
gitfuse_opt_parse( void *data, const char *arg, int key, struct fuse_args *outargs)
{
  if (key == FUSE_OPT_KEY_NONOPT && g_pRepo == NULL)
  {
    if( 0 == git_repository_open( &g_pRepo, arg ) )
    {
      return 0;
    }
  }
  return 1;
}

static struct fuse_operations gitfuse_filesystem_operations = {
    .getattr = gitfuse_getattr, /* To provide size, permissions, etc. */
    .open    = gitfuse_open,    /* To enforce read-only access.       */
    .read    = gitfuse_read,    /* To provide file content.           */
    .readdir = gitfuse_readdir, /* To provide directory listing.      */
};

int
main(int argc, char **argv)
{
  struct fuse_args args = FUSE_ARGS_INIT(argc, argv);
  fuse_opt_parse(&args, NULL, NULL, gitfuse_opt_parse);

  return fuse_main(args.argc, args.argv, &gitfuse_filesystem_operations, NULL);
}

