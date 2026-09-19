#include "core/TerminalBuffer.h"
#include "core/KeyMapping.h"
#include "media/JpegFrame.h"
#include <cassert>
#include <cstring>
#include <iostream>
#include <string>

int main() {
    TerminalBuffer t;
    auto write = [&](const char* text) { t.write(text, strlen(text)); };
    write("hello\rX"); assert(std::string(t.row(0),5)=="Xello");
    write("\033["); write("2;3H!"); assert(t.row(1)[2]=='!');
    write("\033[31mred\033[0m"); assert(std::string(t.row(1)+3,3)=="red");
    write("\033]0;not rendered\033"); write("\\Z"); assert(t.row(1)[6]=='Z');
    write("\033[1;3H\033[K"); assert(std::string(t.row(0),5)=="Xe   ");
    write("\033[2J\033[H"); assert(t.row(1)[2]==' ');
    const std::string exact(TerminalBuffer::Columns,'a'); t.write(exact.data(),exact.size());
    assert(t.line()==0); write("b"); assert(t.line()==1 && t.row(1)[0]=='b');
    t.reset(); write("abc\bZ"); assert(std::string(t.row(0),3)=="abZ");
    t.reset(); write("A\tB"); assert(t.row(0)[8]=='B');
    t.reset(); write("abcde\033[1;2H\033[2P"); assert(std::string(t.row(0),5)=="ade  ");
    write("\033[2@XY"); assert(std::string(t.row(0),5)=="aXYde");
    t.reset();
    for(int i=0;i<13;++i) { auto s=std::to_string(i)+(i<12?"\r\n":""); t.write(s.data(),s.size()); }
    assert(t.row(0)[0]=='1'); assert(std::string(t.row(11),2)=="12");
    t.reset(); write("\033[999999;999999H!"); assert(t.row(11)[37]=='!');
    write("\033[999999A\033[999999D#"); assert(t.row(0)[0]=='#');
    t.reset(); write("x\xc3\xa9z"); assert(std::string(t.row(0),3)=="x?z");
    uint8_t bytes[8]; JpegFrame parser(bytes,sizeof(bytes));
    using R=JpegFrame::Result;
    assert(parser.push(0)==R::More); assert(parser.push(0xff)==R::More);
    assert(parser.push(0xd8)==R::More); parser.push(0x12); parser.push(0xff);
    assert(parser.push(0xd9)==R::Complete && parser.size()==5);
    assert(bytes[0]==0xff && bytes[1]==0xd8 && bytes[4]==0xd9);
    parser.push(0xff); parser.push(0xd8); parser.push(1); parser.push(2); parser.push(3); parser.push(4); parser.push(5); parser.push(6);
    assert(parser.push(7)==R::TooLarge);
    parser.push(0xff); parser.push(0xd8); parser.push(0xff);
    assert(parser.push(0xd9)==R::Complete && parser.size()==4);
    assert(KeyMapping::text(4,false)=='a' && KeyMapping::text(4,true)=='A');
    assert(KeyMapping::text(0x28,false)=='\n' && KeyMapping::text(0x2b,false)=='\t');
    assert(KeyMapping::text(0x33,false)==';' && KeyMapping::text(0x34,true)=='"');
    assert(KeyMapping::text(0x35,false)=='`' && KeyMapping::text(0x38,true)=='?');
    assert(KeyMapping::code(0x33,true)==0x52 && KeyMapping::code(0x37,true)==0x51);
    assert(KeyMapping::code(0x14,true)==0 && KeyMapping::code(0x14,false)==0x14);
    assert(KeyMapping::code(0x2a,true)==0x4c);
    std::cout << "PASS: terminal stream/cursor/scroll, MJPEG bounds/recovery, key mapping\n";
}
