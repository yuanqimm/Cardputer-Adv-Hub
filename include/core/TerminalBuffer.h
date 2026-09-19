#pragma once
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <algorithm>

// Fixed memory, incremental ANSI/VT100 text subset for a 240x135 display.
class TerminalBuffer {
public:
    enum : int { Columns = 38, Rows = 12 };
    TerminalBuffer() { reset(); }
    void reset() {
        for (auto& row : cells_) { memset(row, ' ', Columns); row[Columns] = 0; }
        x_ = y_ = savedX_ = savedY_ = 0; top_ = 0; bottom_ = Rows-1;
        state_ = State::Text; wrap_ = false;
    }
    const char* row(int y) const { return cells_[std::max(0, std::min(Rows-1, y))]; }
    int column() const { return x_; }
    int line() const { return y_; }
    void write(const char* data, size_t size) { for (size_t i=0; i<size; ++i) put(static_cast<uint8_t>(data[i])); }
private:
    enum class State { Text, Escape, Csi, Osc, OscEscape, Charset };
    char cells_[Rows][Columns+1];
    int x_=0,y_=0,savedX_=0,savedY_=0,top_=0,bottom_=Rows-1;
    bool wrap_=false;
    State state_=State::Text;
    int args_[4]={}, arg_=0;
    void blank(int y) { memset(cells_[y], ' ', Columns); }
    void newline() {
        wrap_=false;
        if (y_ == bottom_) {
            for(int y=top_; y<bottom_; ++y) memcpy(cells_[y], cells_[y+1], Columns);
            blank(bottom_);
        } else y_=std::min(Rows-1,y_+1);
    }
    void printable(char ch) {
        if(wrap_) { x_=0; newline(); }
        cells_[y_][x_]=ch;
        if (x_ == Columns-1) wrap_=true; else ++x_;
    }
    int param(int i,int fallback=1) const { return args_[i] ? args_[i] : fallback; }
    void command(uint8_t ch) {
        wrap_=false;
        switch(ch) {
        case 'A': y_=std::max(0,y_-param(0)); break;
        case 'B': y_=std::min(Rows-1,y_+param(0)); break;
        case 'C': x_=std::min(Columns-1,x_+param(0)); break;
        case 'D': x_=std::max(0,x_-param(0)); break;
        case 'G': x_=std::min(Columns-1,param(0)-1); break;
        case 'd': y_=std::min(Rows-1,param(0)-1); break;
        case 'H': case 'f': y_=std::min(Rows-1,param(0)-1); x_=std::min(Columns-1,param(1)-1); break;
        case 'J':
            if(args_[0]>=2) for(int y=0;y<Rows;++y) blank(y);
            else if(args_[0]==0) { memset(cells_[y_]+x_,' ',Columns-x_); for(int y=y_+1;y<Rows;++y) blank(y); }
            else { for(int y=0;y<y_;++y) blank(y); memset(cells_[y_],' ',x_+1); }
            break;
        case 'K':
            if(args_[0]==0) memset(cells_[y_]+x_,' ',Columns-x_);
            else if(args_[0]==1) memset(cells_[y_],' ',x_+1);
            else if(args_[0]==2) blank(y_);
            break;
        case 'P': {
            const int count=std::min(Columns-x_,param(0));
            memmove(cells_[y_]+x_,cells_[y_]+x_+count,Columns-x_-count);
            memset(cells_[y_]+Columns-count,' ',count); break;
        }
        case '@': {
            const int count=std::min(Columns-x_,param(0));
            memmove(cells_[y_]+x_+count,cells_[y_]+x_,Columns-x_-count);
            memset(cells_[y_]+x_,' ',count); break;
        }
        case 'r': {
            const int top=param(0)-1, bottom=param(1,Rows)-1;
            if(top>=0 && bottom<Rows && top<bottom) { top_=top; bottom_=bottom; x_=y_=0; }
            break;
        }
        case 's': savedX_=x_; savedY_=y_; break;
        case 'u': x_=savedX_; y_=savedY_; break;
        default: break; // SGR and unsupported modes are consumed, never rendered.
        }
    }
    void put(uint8_t ch) {
        if(state_==State::Charset) { state_=State::Text; return; }
        if(state_==State::Osc || state_==State::OscEscape) {
            if(ch==7 || (state_==State::OscEscape && ch=='\\')) state_=State::Text;
            else state_=ch==27 ? State::OscEscape : State::Osc;
            return;
        }
        if(state_==State::Escape) {
            state_=State::Text;
            if(ch=='[') { memset(args_,0,sizeof(args_)); arg_=0; state_=State::Csi; }
            else if(ch==']') state_=State::Osc;
            else if(ch=='(' || ch==')') state_=State::Charset;
            else if(ch=='7') { savedX_=x_; savedY_=y_; }
            else if(ch=='8') { x_=savedX_; y_=savedY_; wrap_=false; }
            else if(ch=='D') newline();
            else if(ch=='c') reset();
            return;
        }
        if(state_==State::Csi) {
            if(ch>='0' && ch<='9') args_[arg_]=std::min(999,args_[arg_]*10+ch-'0');
            else if(ch==';') arg_=std::min(3,arg_+1);
            else if(ch>=0x40 && ch<=0x7e) { command(ch); state_=State::Text; }
            else if(ch==27) state_=State::Escape;
            return;
        }
        if(ch==27) state_=State::Escape;
        else if(ch=='\r') { x_=0; wrap_=false; }
        else if(ch=='\n') newline();
        else if(ch=='\b') { x_=std::max(0,x_-1); wrap_=false; }
        else if(ch=='\t') { const int stop=std::min<int>(Columns,((x_/8)+1)*8); while(x_<stop && !wrap_) printable(' '); }
        else if(ch>=32 && ch<127) printable(ch);
        else if(ch>=0xc0) printable('?'); // one replacement per UTF-8 code point
    }
};
