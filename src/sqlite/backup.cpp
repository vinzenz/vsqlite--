#include <sqlite/backup.hpp>
#include <sqlite/connection.hpp>
#include <sqlite/private/private_accessor.hpp>
#include <sqlite/database_exception.hpp>
#include <sqlite3.h>

namespace sqlite {
    backup::backup(connection& conn_to, connection& conn_from)
        : m_pBackup(NULL), m_conn_to(conn_to) {
        private_accessor::acccess_check(conn_to);
        private_accessor::acccess_check(conn_from);

        m_pBackup = sqlite3_backup_init(get_to_handle(), "main",
            private_accessor::get_handle(conn_from), "main");
        if(!m_pBackup) {
            throw database_exception_code(sqlite3_errmsg(get_to_handle()),
                sqlite3_errcode(get_to_handle()));
        }
    }

    backup::~backup() {
        try {
            finish();
        } catch(...) {
        }
    }

    bool backup::step(int nPage) {
        if(!m_pBackup) {
            throw database_exception("Backup object is already destroyed");
        }

        int err = sqlite3_backup_step(m_pBackup, nPage);
        switch(err) {
        case SQLITE_OK:
            return true;
        case SQLITE_DONE:
            return false;
        default:
            throw database_exception_code(sqlite3_errmsg(get_to_handle()), err);
        }
    }

    void backup::finish() {
        if(!m_pBackup) {
            return;
        }

        int err = sqlite3_backup_finish(m_pBackup);
        if(err != SQLITE_OK) {
            throw database_exception_code(sqlite3_errmsg(get_to_handle()), err);
        }
        m_pBackup = NULL;
    }

    sqlite3* backup::get_to_handle() const {
        return private_accessor::get_handle(m_conn_to);
    }
}
