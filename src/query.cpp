#include "localdb/query.hpp"
#include <cctype>

namespace localdb {

static ValueType strToTypeQ(const std::string &s) {
    auto u = toLower(s);
    if (u == "int" || u == "integer") return ValueType::Int;
    if (u == "real" || u == "double" || u == "float") return ValueType::Real;
    if (u == "text" || u == "string" || u == "varchar") return ValueType::Text;
    if (u == "blob") return ValueType::Blob;
    if (u == "bool" || u == "boolean") return ValueType::Bool;
    return ValueType::Text;
}

namespace {

struct Tok { std::string v; bool str=false; };
std::vector<Tok> tokenize(const std::string &sql) {
    std::vector<Tok> t; size_t i=0; 
    while (i < sql.size()) {
        char c = sql[i];
        if (std::isspace((unsigned char)c)) { ++i; continue; }
        if (c=='\'' || c=='"') {
            char q=c; ++i; std::string s;
            while (i<sql.size()) {
                if (sql[i]==q) { if (i+1<sql.size() && sql[i+1]==q) { s+=q; i+=2; } else { ++i; break; } }
                else s+=sql[i++];
            }
            t.push_back({s,true}); continue;
        }
        if (std::isdigit((unsigned char)c) || (c=='-' && i+1<sql.size() && std::isdigit((unsigned char)sql[i+1]))) {
            size_t j=i+1; bool dot=false;
            while (j<sql.size() && (std::isdigit((unsigned char)sql[j]) || (!dot && sql[j]=='.'))) { if (sql[j]=='.') dot=true; ++j; }
            t.push_back({sql.substr(i,j-i),false}); i=j; continue;
        }
        if (std::isalpha((unsigned char)c) || c=='_') {
            size_t j=i+1; while (j<sql.size() && (std::isalnum((unsigned char)sql[j])||sql[j]=='_')) ++j;
            t.push_back({sql.substr(i,j-i),false}); i=j; continue;
        }
        if (c=='<' || c=='>' || c=='!' || c=='=') {
            size_t j=i+1; if (j<sql.size() && sql[j]=='=') ++j;
            t.push_back({sql.substr(i,j-i),false}); i=j; continue;
        }
        t.push_back({std::string(1,c),false}); ++i;
    }
    return t;
}

bool ieq(const std::string &a, const std::string &b) { return toLower(a)==toLower(b); }

Value parseLiteral(const Tok &t) {
    if (t.str) return Value::text(t.v);
    auto l = toLower(t.v);
    if (l=="null") return Value::null();
    if (l=="true") return Value::boolean(true);
    if (l=="false") return Value::boolean(false);
    if (t.v.find('.') != std::string::npos) return Value::real(std::stod(t.v));
    try { return Value::integer(std::stoll(t.v)); } catch(...) { return Value::text(t.v); }
}

} // namespace

ParsedStatement Parser::parse(const std::string &sql) const {
    ParsedStatement ps; ps.raw = sql;
    auto t = tokenize(sql);
    if (t.empty()) return ps;
    size_t i=0;
    auto at=[&](size_t k)->std::string{ return k<t.size()?t[k].v:""; };
    auto atl=[&](size_t k)->std::string{ return toLower(at(k)); };

    if (ieq(at(0),"exit") || ieq(at(0),"quit")) { ps.kind=ParsedStatement::Kind::Exit; return ps; }
    if (ieq(at(0),"help")) { ps.kind=ParsedStatement::Kind::Help; return ps; }
    if (ieq(at(0),"show") && ieq(at(1),"tables")) { ps.kind=ParsedStatement::Kind::ShowTables; return ps; }
    if ((ieq(at(0),"describe") || ieq(at(0),"desc")) && t.size()>=2) { ps.kind=ParsedStatement::Kind::Describe; ps.describeName=at(1); return ps; }

    if (ieq(at(0),"drop") && ieq(at(1),"table")) {
        ps.kind=ParsedStatement::Kind::Drop; i=2;
        if (ieq(at(i),"if") && ieq(at(i+1),"exists")) { ps.drop.ifExists=true; i+=2; }
        if (i<t.size()) ps.drop.name=at(i);
        return ps;
    }
    if (ieq(at(0),"create") && ieq(at(1),"table")) {
        ps.kind=ParsedStatement::Kind::Create; i=2;
        if (ieq(at(i),"if") && ieq(at(i+1),"not") && ieq(at(i+2),"exists")) { ps.create.ifNotExists=true; i+=3; }
        if (i<t.size()) ps.create.name=at(i++);
        // find ( ... )
        while (i<t.size() && at(i)!="(") ++i;
        if (i<t.size() && at(i)=="(") ++i;
        while (i<t.size() && at(i)!=")") {
            if (at(i)==",") { ++i; continue; }
            Column c; c.name = at(i++);
            if (i<t.size() && at(i)!="," && at(i)!=")") { c.type = strToTypeQ(at(i++)); }
            while (i<t.size() && at(i)!="," && at(i)!=")") {
                if (ieq(at(i),"primary") && ieq(at(i+1),"key")) { c.primaryKey=true; c.notNull=true; c.unique=true; c.indexed=true; i+=2; }
                else if (ieq(at(i),"not") && ieq(at(i+1),"null")) { c.notNull=true; i+=2; }
                else if (ieq(at(i),"unique")) { c.unique=true; c.indexed=true; ++i; }
                else if (ieq(at(i),"index") || ieq(at(i),"indexed")) { c.indexed=true; ++i; }
                else if (ieq(at(i),"default") && i+1<t.size()) { c.defaultValue = parseLiteral(t[i+1]); i+=2; }
                else ++i;
            }
            if (!c.name.empty()) ps.create.columns.push_back(std::move(c));
        }
        return ps;
    }
    if (ieq(at(0),"insert") && ieq(at(1),"into")) {
        ps.kind=ParsedStatement::Kind::Insert; ps.insert.table=at(2); i=3;
        if (i<t.size() && at(i)=="(") {
            ++i;
            while (i<t.size() && at(i)!=")") { if (at(i)!=",") ps.insert.columns.push_back(at(i)); ++i; }
            if (i<t.size()) ++i;
        }
        if (ieq(at(i),"values")) ++i;
        if (i<t.size() && at(i)=="(") ++i;
        while (i<t.size() && at(i)!=")") { if (at(i)!=",") ps.insert.values.push_back(parseLiteral(t[i])); ++i; }
        return ps;
    }
    if (ieq(at(0),"select")) {
        ps.kind=ParsedStatement::Kind::Select; i=1;
        while (i<t.size() && !ieq(at(i),"from")) { if (at(i)!=",") ps.select.columns.push_back(at(i)); ++i; }
        if (ieq(at(i),"from")) ++i;
        if (i<t.size()) ps.select.table=at(i++);
        auto parseConds=[&](std::vector<Condition>&conds){
            while (i<t.size()) {
                if (ieq(at(i),"and")) { ++i; continue; }
                if (ieq(at(i),"order") || ieq(at(i),"limit")) break;
                if (i+2<t.size()) {
                    Condition c; c.column=at(i);
                    std::string op=at(i+1);
                    if (ieq(op,"is") && ieq(at(i+2),"null")) { c.op=CmpOp::IsNull; i+=3; }
                    else if (ieq(op,"is") && ieq(at(i+2),"not") && ieq(at(i+3),"null")) { c.op=CmpOp::NotNull; i+=4; }
                    else if (ieq(op,"like")) { c.op=CmpOp::Like; c.value=parseLiteral(t[i+2]); i+=3; }
                    else {
                        if (op=="=") c.op=CmpOp::Eq; else if (op=="!="||op=="<>") c.op=CmpOp::Ne;
                        else if (op=="<") c.op=CmpOp::Lt; else if (op=="<=") c.op=CmpOp::Le;
                        else if (op==">") c.op=CmpOp::Gt; else if (op==">=") c.op=CmpOp::Ge;
                        else c.op=CmpOp::Eq;
                        c.value=parseLiteral(t[i+2]); i+=3;
                    }
                    conds.push_back(std::move(c));
                } else break;
            }
        };
        if (ieq(at(i),"where")) { ++i; parseConds(ps.select.where); }
        if (ieq(at(i),"order") && ieq(at(i+1),"by")) { i+=2; if(i<t.size()) ps.select.orderBy=at(i++); if(ieq(at(i),"desc")) {ps.select.desc=true;++i;} else if(ieq(at(i),"asc")) ++i; }
        if (ieq(at(i),"limit") && i+1<t.size()) { ps.select.limit = std::stoull(at(i+1)); i+=2; }
        return ps;
    }
    if (ieq(at(0),"delete") && ieq(at(1),"from")) {
        ps.kind=ParsedStatement::Kind::Delete; ps.del.table=at(2); i=3;
        if (ieq(at(i),"where")) { ++i;
            while (i<t.size()) {
                if (ieq(at(i),"and")) { ++i; continue; }
                if (i+2<t.size()) {
                    Condition c; c.column=at(i); std::string op=at(i+1);
                    if (ieq(op,"is") && ieq(at(i+2),"null")) { c.op=CmpOp::IsNull; i+=3; }
                    else if (ieq(op,"is") && ieq(at(i+2),"not") && ieq(at(i+3),"null")) { c.op=CmpOp::NotNull; i+=4; }
                    else { c.op = (op=="=")?CmpOp::Eq:(op=="!="||op=="<>")?CmpOp::Ne:(op=="<")?CmpOp::Lt:(op=="<=")?CmpOp::Le:(op==">")?CmpOp::Gt:CmpOp::Ge; c.value=parseLiteral(t[i+2]); i+=3; }
                    ps.del.where.push_back(std::move(c));
                } else break;
            }
        }
        return ps;
    }
    if (ieq(at(0),"update")) {
        ps.kind=ParsedStatement::Kind::Update; ps.update.table=at(1); i=2;
        if (ieq(at(i),"set")) ++i;
        while (i<t.size() && !ieq(at(i),"where")) {
            if (at(i)==",") { ++i; continue; }
            if (i+2<t.size() && at(i+1)=="=") { ps.update.sets[toLower(at(i))]=parseLiteral(t[i+2]); i+=3; }
            else ++i;
        }
        if (ieq(at(i),"where")) { ++i;
            while (i<t.size()) {
                if (ieq(at(i),"and")) { ++i; continue; }
                if (i+2<t.size()) {
                    Condition c; c.column=at(i); std::string op=at(i+1);
                    c.op = (op=="=")?CmpOp::Eq:(op=="!="||op=="<>")?CmpOp::Ne:(op=="<")?CmpOp::Lt:(op=="<=")?CmpOp::Le:(op==">")?CmpOp::Gt:CmpOp::Ge;
                    c.value=parseLiteral(t[i+2]); ps.update.where.push_back(std::move(c)); i+=3;
                } else break;
            }
        }
        return ps;
    }
    return ps;
}

} // namespace localdb
