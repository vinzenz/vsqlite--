/*
** 2005 February 15
**
** The author disclaims copyright to this source code.  In place of
** a legal notice, here is a blessing:
**
**    May you do good and not evil.
**    May you find forgiveness for yourself and forgive others.
**    May you share freely, never taking more than you give.
**
*************************************************************************
** This file contains C code routines that used to generate VDBE code
** that implements the ALTER TABLE command.
**
** $Id: alter.c,v 1.20 2006/02/09 02:56:03 drh Exp $
*/
#include "sqliteInt.h"

int sqlite3_clear_bindings(sqlite3_stmt *pStmt){
      int i;
      int rc = SQLITE_OK;
      for(i=1; rc==SQLITE_OK && i<=sqlite3_bind_parameter_count(pStmt); i++){
            rc = sqlite3_bind_null(pStmt, i);
          }
      return rc;
    }