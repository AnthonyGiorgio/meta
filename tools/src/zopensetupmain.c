#include <stdio.h>
#include "download.h"
#include "zopenio.h"
#include "createpem.h"
#include "createdb.h"
#include "createdirs.h"
#include "github_pem_ca.h"
#include "zopen_boot_uri.h"
#include "httpsget.h"
#include "syscmd.h"
#include "createbootenv.h"

#define MAIN
#include "options.h"

static const char* lastpos(const char* str, int c) {
  int i;
  for (i=strlen(str); i>=0; --i) {
    if (str[i] == c) {
      return &str[i];
    }
  }
  return NULL;
}
 
static void syntax(const char* pgm) {
  const char* base;
  if (base = lastpos(pgm, '/')) {
    pgm = &base[1];
  }
  fprintf(stderr, "%s : Install the z/OS Open Source Tools 'starter' environment by downloading from %s\n"
                  "Syntax: %s [-vq] <root>\n"
                  "  <root> is the root directory where:\n"
                  "    a symbolic link from ${HOME}/zopen to <root> will be created\n"
                  "    boot, prod, and dev directories will be created\n"
                  "  The boot subdirectory will have:\n"
                  "    sub-directories created for each of the tools needed for running the zopen utility\n"
                  "  The dev subdirectory will have:\n"
                  "    a 'git clone' of both the utils and meta repositories\n" 
                  "Options:\n"
                  " -v : print out verbose messages\n"
                  " -q : only print out errors\n",
                  pgm, ZOPEN_TOOLS_URL, pgm);
  return;
}

/*
 * This program does the following:
 * - creates the directories for z/OS Open Source Tools from the 'root' directory provided (boot, prod, dev)
 * - creates a temporary PEM file that will be used to connect to the github.com site to download bootstrap tools (e.g. curl)
 * - download each of the packages into the bootstrap directory as pax.Z files
 * - unpax the pax files
 * - generate a .bootenv script that can later be sourced in the 'boot' directory for subsequent setup of the boot environment
 * - create symbolic link from $HOME/zopen to 'root' directory provided
 */

int main(int argc, char* argv[]) {
  const char* host = "github.com";
  const char* bootpkg[] = ZOPEN_BOOT_PKG;
  const char* pemdata = GITHUB_PEM_CA;
  char* pkgsfx;
  char  root[ZOPEN_PATH_MAX+1];
  char  filename[ZOPEN_PATH_MAX+1];
  char  output[ZOPEN_PATH_MAX+1];
  char  tmppem[ZOPEN_PATH_MAX+1];
  char  uri[ZOPEN_PATH_MAX+1];
  int   rc;
  int   i;
  int parmsok=0;

  if (argc < 2) {
    syntax(argv[0]);
    return 4; 
  }
  for (i=1; i<argc; ++i) {
    if (!strcmp(argv[i], "-v")) {
      verbose = 1;
    } else if  (!strcmp(argv[i], "-q"))	{
      verbose =	0;
    } else if (argv[i][0] == '-') {
      fprintf(stderr, "Unknown option: %s specified\n", argv[i]);
      syntax(argv[0]);
      return 8;
    } else if (i != (argc-1)) {
      fprintf(stderr, "Too many parameters specified\n");
      syntax(argv[0]);
      return 8;
    } else {
      parmsok=1;
    }
  }
  if (!parmsok) {
    fprintf(stderr, "Specify a directory to install into\n");
    syntax(argv[0]);
    return 8;
  }
  if (!realpath(argv[argc-1], root)) {
    fprintf(stderr, "Directory %s does not exist, or is not writable\n", argv[argc-1]);
    syntax(argv[0]);
    return 4;
  }
 
  if (!tmppem || genfilename("pem", tmppem, ZOPEN_PATH_MAX)) {
    fprintf(stderr, "error acquiring storage\n");
    return 4;
  }

  if (verbose) {
    fprintf(STDTRC, "Creating directories under %s\n", root);
  }

  if (rc = createdirs(root))  {
    fprintf(stderr, "error creating directories: %d\n", rc);
    return rc;
  }

  if (rc = createpem(pemdata, tmppem)) {
    fprintf(stderr, "error creating pem file: %d\n", rc);
    return rc;
  }
  for (i=0; bootpkg[i]; ++i) {
    if (strcmp(bootpkg[i], "utils")) {
      pkgsfx="port";
    } else {
      pkgsfx="";
    }

    if (verbose) {
      fprintf(STDTRC, "Download %s into %s/%s\n", bootpkg[i], root,  ZOPEN_BOOT);
    }
    if (getfilenamefrompkg(bootpkg[i], pkgsfx, tmppem, filename, ZOPEN_PATH_MAX)) {
      fprintf(stderr, "error acquiring storage (3)\n");
      return 4;
    }
    if (genfilenameinsubdir(root, ZOPEN_BOOT, filename, output, ZOPEN_PATH_MAX)) {
      fprintf(stderr, "error acquiring storage (2)\n");
      return 4;
    }
    if ((rc = snprintf(uri, sizeof(uri), "/%s/%s%s/%s/%s", ZOPEN_BOOT_URI_PREFIX, bootpkg[i], pkgsfx, ZOPEN_BOOT_URI_SUFFIX, filename)) > sizeof(uri)) {
      fprintf(stderr, "error building uri for /%s/%s%s/%s/%s", ZOPEN_BOOT_URI_PREFIX, bootpkg[i], pkgsfx, ZOPEN_BOOT_URI_SUFFIX, filename);
      return 4;
    }   
    if (rc = httpsget(host, uri, tmppem, output)) {
      fprintf(stderr, "error downloading https://%s%s with PEM file %s to %s\n", host, uri, tmppem, output);
      return rc;
    }
    if (rc = unpaxandlink(root, ZOPEN_BOOT, output, bootpkg[i])) {
      fprintf(stderr, "error unpaxing %s in directory %s/%s\n", output, root, ZOPEN_BOOT);
      return rc;
    }
  }

  if (verbose) {
    fprintf(STDTRC, "Create %s for source'ing in %s/%s\n", ZOPEN_BOOT_ENV, root, ZOPEN_BOOT);
  }
  if (rc = createbootenv(root, ZOPEN_BOOT, bootpkg)) {
    fprintf(stderr, "error creating %s in directory %s/%s\n", ZOPEN_BOOT_ENV, root, ZOPEN_BOOT);
    return rc;
  }

  if (verbose) {
    fprintf(STDTRC, "Create symbolic link from %s/%s to %s\n", ZOPEN_HOME, ZOPEN_HOME_NAME, root);
  }
  if (createhomelink(ZOPEN_HOME, ZOPEN_HOME_NAME, root)) {
    fprintf(stderr, "error creating symbolic link from %s/%s to %s\n", ZOPEN_HOME, ZOPEN_HOME_NAME, root);
    return rc;
  }
   
  if (remove(tmppem)) {
    fprintf(stderr, "error removing temporary pem file: %s\n", tmppem);
    return 4;
  }
  return 0;
}
