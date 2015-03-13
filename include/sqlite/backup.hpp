#ifndef GUARD_SQLITE_BACKUP_HPP_INCLUDED
#define GUARD_SQLITE_BACKUP_HPP_INCLUDED

#include <boost/noncopyable.hpp>
#include <sqlite/connection.hpp>

struct sqlite3_backup;

namespace sqlite {
    struct backup: boost::noncopyable {
        backup(connection& conn_to, connection& conn_from);
        ~backup();
        bool step(int nPage = -1);
        void finish();
    private:
        sqlite3* get_to_handle() const;
    private:
        sqlite3_backup* m_pBackup;
        connection& m_conn_to;
    };
}
#endif // #ifndef GUARD_SQLITE_BACKUP_HPP_INCLUDED
