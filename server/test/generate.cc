

#include <log.hh>
#include "user.hh"
#include "user-odb.hxx"
#include <odb/mysql/database.hxx>
#include <odb/mysql/connection.hxx>
#include <mysql.hh>
#include <random>

static int number(size_t min, size_t max) {
    // 1. ⽣成⼀个机器随机数，作为伪随机数种⼦
    static std::random_device rd;
    // 2. 根据种⼦，构造伪随机数引擎
    static std::mt19937 generator(rd());
    // 3. ⽣成伪随机数
    std::uniform_int_distribution<int> distribution(min, max);
    return distribution(generator);
}

int main(){
    std::shared_ptr<odb::mysql::database> db_ = mysql::mysql_build::build(
            "ltx",
            "544338",
            "game_demo",
            "127.0.0.1",
            3306,
            "utf8",
            5);
    try{
        odb::transaction t(db_->begin());
        for(int i = 1000; i < 2000; i++){
            User user(
                "user" + std::to_string(i),
                "123123",
                i,
                number(-500000, 500000) / 100.0f,
                number(-500000, 500000) / 100.0f,
                0,
                number(0, 36000) / 100.0f,
                number(-9000, 9000) / 100.0f,
                0
            );
            db_->persist(user);
        }
        t.commit();
    }catch(const std::exception& e){
        errorlog << "failed to insert user info : " << e.what();
    }
    
    return 0;
}