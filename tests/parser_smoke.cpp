#include "vicvpn/parser/UriParser.h"
#include "vicvpn/parser/ImportService.h"
#include <QCoreApplication>
#include <QByteArray>
#include <QString>
#include <cstdio>

using namespace vicvpn;

static int g_fail = 0;

static void check(bool cond, const QString& what) {
    std::printf("%s  %s\n", cond ? "[ OK ]" : "[FAIL]", what.toUtf8().constData());
    if (!cond)
        g_fail++;
}

int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);
    g_fail = 0;

    {
        const auto p = UriParser::parse("ssh://root:s3cret@1.2.3.4:2222#MySsh");
        check(p.has_value(), "ssh:// parses");
        if (p) {
            check(p->protocol == Protocol::Ssh, "ssh protocol enum");
            check(p->ssh.host == "1.2.3.4", "ssh host == 1.2.3.4");
            check(p->ssh.port == 2222, "ssh port == 2222");
            check(p->ssh.user == "root", "ssh user == root");
            check(p->ssh.password == "s3cret", "ssh password == s3cret");
            check(p->name == "MySsh", "ssh name from fragment");
        }
    }

    {
        const auto p = UriParser::parse("ssh://root@10.0.0.9");
        check(p.has_value() && p->ssh.port == 22, "ssh default port 22");
    }

    {
        const auto p = UriParser::parse("ssh://root:pw@1.2.3.4?privateKey=C:/keys/id_ed25519");
        check(p.has_value() && p->ssh.privateKeyPath == "C:/keys/id_ed25519", "ssh privateKey query");
    }

    {
        const QString plain = "vless://11111111-1111-1111-1111-111111111111@1.1.1.1:443?type=ws&security=tls#A\n"
                              "trojan://pw@2.2.2.2:443#B";
        const auto list = SubscriptionBodyDecoder::decode(plain.toUtf8());
        check(list.size() == 2, "plain URI list -> 2 profiles");
    }

    {
        const QString plain = "vless://11111111-1111-1111-1111-111111111111@1.1.1.1:443?type=ws&security=tls#A\n"
                              "trojan://pw@2.2.2.2:443#B";
        const auto list = SubscriptionBodyDecoder::decode(plain.toUtf8().toBase64());
        check(list.size() == 2, "base64 subscription -> 2 profiles");
    }

    {
        const QString json = R"({"outbounds":[{"protocol":"trojan","tag":"t1","settings":{"servers":[{"address":"9.9.9.9","port":443,"password":"pw"}]}}]})";
        const auto list = SubscriptionBodyDecoder::decode(json.toUtf8());
        check(list.size() == 1, "JSON subscription -> 1 profile");
    }

    {
        QString err;
        const auto list = SubscriptionBodyDecoder::decode("<html>error</html>", &err);
        check(list.empty(), "garbage body -> empty");
        check(!err.isEmpty(), "garbage body -> error message set");
    }

    {
        QString err;
        const auto list = SubscriptionBodyDecoder::decode(QByteArray(), &err);
        check(list.empty() && !err.isEmpty(), "empty body -> error message set");
    }

    {
        QString err;
        const auto list = ImportService::importText("ssh://u:p@h.example.com:22#N", &err);
        check(list.size() == 1 && list[0].protocol == Protocol::Ssh, "importText routes ssh://");
    }

    {
        const QString outline = "ss://Y2hhY2hhMjAtaWV0Zi1wb2x5MTMwNTpLRVdrMzJ2c09IMlI2Qkd5U3pmcVlJ@216.105.168.18:443"
                                "/?outline=1&prefix=%16%03%01%00%C2%A8%01%01#\xF0\x9F\x87\xBA\xF0\x9F\x87\xB8 US @OutlineVPN_ru";
        const auto p = UriParser::parse(outline);
        check(p.has_value(), "Outline base64 ss:// parses");
        if (p) {
            check(p->protocol == Protocol::Shadowsocks, "outline ss protocol");
            check(p->xrayOutbound["settings"]["servers"][0]["address"] == "216.105.168.18",
                  "outline ss host");
            check(p->xrayOutbound["settings"]["servers"][0]["port"] == 443, "outline ss port");
            check(p->xrayOutbound["settings"]["servers"][0]["method"] ==
                      "chacha20-ietf-poly1305",
                  "outline ss method decoded from base64");
            const std::string pw =
                p->xrayOutbound["settings"]["servers"][0]["password"].get<std::string>();
            check(pw == "KEWk32vsOH2R6BGySzfqYI", "outline ss password decoded from base64");
            const std::string prefix =
                p->xrayOutbound["settings"]["servers"][0]["prefix"].get<std::string>();
            std::string expected;
            for (unsigned char c : {0x16, 0x03, 0x01, 0x00, 0xC2, 0xA8, 0x01, 0x01})
                expected.push_back(static_cast<char>(c));
            check(prefix == expected, "outline ss binary prefix preserved byte-for-byte");
            check(p->name.contains("OutlineVPN_ru"), "outline ss name from fragment");
        }
    }

    {
        const auto p = UriParser::parse("ss://aes-256-gcm:pwd@example.com:8388#Plain");
        check(p.has_value() && p->xrayOutbound["settings"]["servers"][0]["method"] == "aes-256-gcm" &&
                  p->xrayOutbound["settings"]["servers"][0]["password"] == "pwd",
              "SIP002 plain method:password@host:port still works");
    }

    {
        const QString legacy = "ss://" +
                               QString::fromUtf8(QByteArray("aes-128-gcm:secret@1.2.3.4:9000").toBase64());
        const auto p = UriParser::parse(legacy);
        check(p.has_value() &&
                  p->xrayOutbound["settings"]["servers"][0]["address"] == "1.2.3.4" &&
                  p->xrayOutbound["settings"]["servers"][0]["port"] == 9000,
              "legacy base64 method:password@host:port still works");
    }

    std::printf("\n%s: %d failure(s)\n", g_fail == 0 ? "PASS" : "FAIL", g_fail);
    return g_fail == 0 ? 0 : 1;
}