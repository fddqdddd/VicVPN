#include "vicvpn/parser/UriParser.h"
#include "vicvpn/parser/ImportService.h"
#include <QCoreApplication>
#include <QByteArray>
#include <QString>
#include <QUrl>
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

    std::printf("==== QUrl diagnostics ====\n");
    const QStringList samples = {
        QStringLiteral("vless://11111111-1111-1111-1111-111111111111@1.1.1.1:443?type=ws&security=tls#Name"),
        QStringLiteral("ss://Y2hhY2hhMjAtaWV0Zi1wb2x5MTMwNTpLRVdrMzJ2c09IMlI2Qkd5U3pmcVlJ@216.105.168.18:443"
                       "/?outline=1&prefix=%16%03%01%00%C2%A8%01%01#\xF0\x9F\x87\xBA\xF0\x9F\x87\xB8 US @OutlineVPN_ru"),
        QStringLiteral("trojan://password@example.net:443?sni=a.b#My Node"),
        QStringLiteral("socks://1.2.3.4:1080#Socks"),
        QStringLiteral("vmess://eyJ2IjoiMiIsInBzIjoidGVzdCJ9"),
        QStringLiteral("hy2://pw@example.org:443#HY"),
    };
    for (const QString& s : samples) {
        const QUrl u(s);
        std::printf("scheme=%-8s valid=%d host=%-16s port=%-5d user=%-12.12s pass=%-10.10s frag=%s\n",
                    u.scheme().toUtf8().constData(),
                    static_cast<int>(u.isValid()),
                    u.host().toUtf8().constData(),
                    u.port(-1),
                    u.userName().toUtf8().constData(),
                    u.password().toUtf8().constData(),
                    u.fragment().toUtf8().constData());
    }
    std::printf("==== end diagnostics ====\n\n");

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

    {
        const QString vmessJson = QStringLiteral(
            R"({"v":"2","ps":"My VMess","add":"example.com","port":"443","id":"11111111-1111-1111-1111-111111111111","aid":0,"scy":"auto","net":"ws","type":"none","host":"cdn.example.com","path":"/ws","tls":"tls"})");
        const QString link = "vmess://" + QString::fromUtf8(vmessJson.toUtf8().toBase64()) + "#ignored";
        const auto p = UriParser::parse(link);
        check(p.has_value(), "vmess:// base64+fragment parses");
        if (p) {
            check(p->protocol == Protocol::Vmess, "vmess protocol");
            check(p->xrayOutbound["settings"]["vnext"][0]["address"] == "example.com", "vmess address");
            check(p->xrayOutbound["settings"]["vnext"][0]["port"] == 443, "vmess port");
            check(p->name == "My VMess", "vmess name from ps");
            check(p->xrayOutbound["streamSettings"]["network"] == "ws", "vmess ws network");
            check(p->xrayOutbound["streamSettings"]["tlsSettings"]["serverName"] ==
                      "cdn.example.com",
                  "vmess tls serverName falls back to host");
        }
    }

    {
        const auto p = UriParser::parse(
            "trojan://PaSs@ex.example.net:8443?sni=s.example.net&type=tcp#Trojan");
        check(p.has_value(), "trojan:// parses");
        if (p) {
            check(p->xrayOutbound["settings"]["servers"][0]["password"] == "PaSs",
                  "trojan password from userinfo");
            check(p->xrayOutbound["settings"]["servers"][0]["port"] == 8443, "trojan port");
            check(p->xrayOutbound["streamSettings"]["tlsSettings"]["serverName"] == "s.example.net",
                  "trojan sni");
        }
    }

    {
        const auto p = UriParser::parse("SOCKS5://user:pw@5.6.7.8:1080#S");
        check(p.has_value(), "SOCKS5:// uppercase scheme parses");
        if (p) {
            check(p->protocol == Protocol::Socks, "socks protocol");
            check(p->xrayOutbound["settings"]["servers"][0]["port"] == 1080, "socks port");
        }
    }

    {
        const auto p = UriParser::parse("hy2://secret@9.8.7.6:443?sni=a.b&insecure=1#HY2");
        check(p.has_value() && p->core == CoreType::Hysteria2, "hy2:// parses");
    }

    {
        // Regression: many generators emit "port" as a number and "tls" as a bool.
        const QString vmessJson = QStringLiteral(
            R"({"v":"2","ps":"NumericPort","add":"n.example.com","port":2053,"id":"22222222-2222-2222-2222-222222222222","aid":"0","net":"tcp","tls":true})");
        const auto p = UriParser::parse(
            "vmess://" + QString::fromUtf8(vmessJson.toUtf8().toBase64()));
        check(p.has_value(), "vmess with numeric port parses");
        if (p) {
            check(p->xrayOutbound["settings"]["vnext"][0]["port"] == 2053,
                  "vmess numeric port preserved");
            check(p->xrayOutbound["streamSettings"]["security"] == "tls",
                  "vmess boolean tls enabled");
        }
    }

    {
        QString err;
        const auto list = ImportService::importText("ftp://example.com/key", &err);
        check(list.empty(), "unknown scheme -> empty");
        check(err.contains("ftp://"), "unsupported scheme error names the scheme");
    }

    {
        QString err;
        const auto list = ImportService::importText("just some random text", &err);
        check(list.empty() && !err.isEmpty(), "plain garbage -> helpful error");
    }

    {
        QString err;
        const auto list = ImportService::importText(
            "trojan://a@b.com:443#ok\ntotally broken line\nvmess://" +
                QString::fromUtf8(
                    QStringLiteral(R"({"add":"x.com","port":443,"id":"u"})").toUtf8().toBase64()),
            &err);
        check(list.size() == 2, "bulk import skips bad lines and keeps good ones");
    }

    {
        QString err;
        QString blob = QStringLiteral("trojan://a@b.com:443#1\ntrojan://a@c.com:443#2");
        blob.prepend(QChar(0xFEFF));
        const auto list = UriParser::parseMany(blob);
        check(list.size() == 2, "BOM before first key is stripped");
    }

    std::printf("\n%s: %d failure(s)\n", g_fail == 0 ? "PASS" : "FAIL", g_fail);
    return g_fail == 0 ? 0 : 1;
}