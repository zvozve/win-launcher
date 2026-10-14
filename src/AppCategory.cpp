#include "AppCategory.h"

namespace {

struct Rule
{
    QString key;
    QStringList keywords;
};

/*
 * 规则表。顺序 = 匹配优先级。
 * 分类顺序：开发 > 影音 > 办公 > 网络 > 工具。
 *
 * 为什么是这个顺序（每种反例都真实存在）：
 *   - 「开发」在「工具」前：Visual Studio Code / JetBrains 系列都像工具，实际归开发；
 *   - 「影音」在「网络」前：QQ音乐 含 "qq"，不能因为 "qq" 被归到网络；
 *   - 「办公」在「网络」前：微信输入法 含 "微信"，应该归办公而不是网络
 *     （所以 office 里用的是「输入法」而不是「微信」）；
 *   - 「工具」放最后：它的关键词最泛（driver / 管理 / 助手），放前面会吃掉一大片。
 *
 * 关键词都是小写片段，对「名字 + 目标路径」整体做 contains。
 * 粗一点没关系 —— 判错会落到「其他」，比漏判容易发现。
 */
const QList<Rule> &rules()
{
    static const QList<Rule> table = {
        {QStringLiteral("dev"),
         {
             // IDE / 编辑器
             QStringLiteral("visual studio"), QStringLiteral("vscode"),
             QStringLiteral("vs code"), QStringLiteral("pycharm"), QStringLiteral("clion"),
             QStringLiteral("intellij"), QStringLiteral("idea64"),
             QStringLiteral("android studio"), QStringLiteral("codeblocks"),
             QStringLiteral("source insight"), QStringLiteral("sublime merge"),
             // Qt 全家桶（"\\qt\\" 兜住 C:\Qt\5.15.2\... 这种光看名字看不出的）
             QStringLiteral("qt creator"), QStringLiteral("qt maintenance"),
             QStringLiteral("qt designer"), QStringLiteral("qt assistant"),
             QStringLiteral("qt linguist"), QStringLiteral("qt tools"),
             QStringLiteral("\\qt\\"),
             // 构建 / 编译器
             QStringLiteral("cmake"), QStringLiteral("ninja"), QStringLiteral("gcc"),
             QStringLiteral("g++"), QStringLiteral("mingw"), QStringLiteral("msys"),
             QStringLiteral("cygwin"), QStringLiteral("clang"), QStringLiteral("llvm"),
             QStringLiteral("arm gnu"), QStringLiteral("toolchain"),
             QStringLiteral("rustup"), QStringLiteral("cargo"), QStringLiteral("dotnet"),
             QStringLiteral("jdk"), QStringLiteral("jre"),
             // 语言运行时
             QStringLiteral("python"), QStringLiteral("node.js"), QStringLiteral("nodejs"),
             QStringLiteral("npm"), QStringLiteral("perl"), QStringLiteral("ruby"),
             // 版本控制
             QStringLiteral("git"), QStringLiteral("github"), QStringLiteral("tortoise"),
             QStringLiteral("subversion"), QStringLiteral("sourcetree"),
             // 虚拟化 / 子系统
             QStringLiteral("docker"), QStringLiteral("wsl"), QStringLiteral("ubuntu"),
             QStringLiteral("virtualbox"), QStringLiteral("vmware"),
             // 嵌入式 / 硬件
             QStringLiteral("keil"), QStringLiteral("uvision"), QStringLiteral("mdk"),
             QStringLiteral("stm32"), QStringLiteral("cubemx"), QStringLiteral("cubeide"),
             QStringLiteral("cube programmer"), QStringLiteral("iar"),
             QStringLiteral("segger"), QStringLiteral("j-link"), QStringLiteral("st-link"),
             QStringLiteral("openocd"), QStringLiteral("arduino"),
             QStringLiteral("platformio"), QStringLiteral("esp-idf"),
             QStringLiteral("espressif"), QStringLiteral("rt-thread"),
             QStringLiteral("altium"), QStringLiteral("lceda"), QStringLiteral("立创eda"),
             QStringLiteral("kicad"), QStringLiteral("proteus"), QStringLiteral("multisim"),
             QStringLiteral("ltspice"), QStringLiteral("vivado"), QStringLiteral("quartus"),
             QStringLiteral("mplab"),
             // 终端 / 串口 / 抓包调试
             QStringLiteral("terminal"), QStringLiteral("powershell"),
             QStringLiteral("xshell"), QStringLiteral("mobaxterm"), QStringLiteral("putty"),
             QStringLiteral("securecrt"), QStringLiteral("termius"),
             QStringLiteral("serial"), QStringLiteral("串口"), QStringLiteral("vofa"),
             QStringLiteral("sscom"), QStringLiteral("xcom"), QStringLiteral("modbus"),
             QStringLiteral("mqtt"),
             // 仪器 / 打包 / 芯片工具（NI 那 7 个条目靠 "national instruments" 一次盖住）
             QStringLiteral("labview"), QStringLiteral("national instruments"),
             QStringLiteral("datasocket"), QStringLiteral("vipm"), QStringLiteral("kingst"),
             QStringLiteral("inno setup"), QStringLiteral("enigma virtual box"),
             QStringLiteral("pl2303"), QStringLiteral("pl23xx"), QStringLiteral("prolific"),
             QStringLiteral("正点原子"), QStringLiteral("脱机下载"), QStringLiteral("cc switch"),
             QStringLiteral("vi package manager"), QStringLiteral("xinje"),
             QStringLiteral("touchwin"), QStringLiteral("visualtft"), QStringLiteral("odbc"),
             QStringLiteral("development kit"), QStringLiteral("application verifier"),
             // SDK 出的"包管理器"之类也带 development kit / pcap
             QStringLiteral("pcap"),
             // 光看名字认不出来的："Qt 5.15.2 (MSVC 2019 32-bit)" 的 target 里没有
             // 可用的路径线索，只能靠名字里的 qt 兜。风险很低（"qt" 这个子串很少见）。
             QStringLiteral("qt"),
             // 数据库 / 接口
             QStringLiteral("postman"), QStringLiteral("dbeaver"), QStringLiteral("navicat"),
             QStringLiteral("redis"), QStringLiteral("mysql"), QStringLiteral("sqlite"),
             // 游戏引擎 / 逆向
             QStringLiteral("unity"), QStringLiteral("unreal"), QStringLiteral("godot"),
             QStringLiteral("ghidra"), QStringLiteral("windbg"),
         }},
        {QStringLiteral("media"),
         {
             QStringLiteral("vlc"), QStringLiteral("potplayer"), QStringLiteral("mpv"),
             QStringLiteral("kmplayer"), QStringLiteral("obs studio"),
             QStringLiteral("剪映"), QStringLiteral("premiere"), QStringLiteral("after effects"),
             QStringLiteral("photoshop"), QStringLiteral("illustrator"),
             QStringLiteral("gimp"), QStringLiteral("inkscape"), QStringLiteral("blender"),
             QStringLiteral("audacity"), QStringLiteral("foobar"),
             QStringLiteral("网易云"), QStringLiteral("qq音乐"), QStringLiteral("酷狗"),
             QStringLiteral("酷我"), QStringLiteral("spotify"),
             QStringLiteral("爱奇艺"), QStringLiteral("bilibili"), QStringLiteral("哔哩"),
             QStringLiteral("格式工厂"), QStringLiteral("handbrake"),
             QStringLiteral("播放器"), QStringLiteral("剪辑"), QStringLiteral("录屏"),
             QStringLiteral("camtasia"), QStringLiteral("xmedia"), QStringLiteral("mediainfo"),
             QStringLiteral("gigabyte control"), QStringLiteral("nahimic"),
             // 媒体播放 / 看图 / 三维建模
             QStringLiteral("media player"), QStringLiteral("imageglass"),
             QStringLiteral("看图"), QStringLiteral("阅读器"), QStringLiteral("koodo"),
             QStringLiteral("freecad"), QStringLiteral("autocad"),
             QStringLiteral("solidworks"), QStringLiteral("cadence"),
         }},
        {QStringLiteral("office"),
         {
             QStringLiteral("office"), QStringLiteral("word"), QStringLiteral("excel"),
             QStringLiteral("powerpoint"), QStringLiteral("wps"), QStringLiteral("onenote"),
             QStringLiteral("notion"), QStringLiteral("obsidian"), QStringLiteral("typora"),
             QStringLiteral("笔记"), QStringLiteral("xmind"), QStringLiteral("mindmaster"),
             QStringLiteral("draw.io"), QStringLiteral("visio"),
             QStringLiteral("notepad"), QStringLiteral("sublime"),
             QStringLiteral("pdf"), QStringLiteral("acrobat"), QStringLiteral("foxit"),
             QStringLiteral("sumatra"), QStringLiteral("calibre"), QStringLiteral("zotero"),
             QStringLiteral("输入法"), QStringLiteral("拼音"), QStringLiteral("五笔"),
             QStringLiteral("有道"), QStringLiteral("词典"), QStringLiteral("翻译"),
             QStringLiteral("outlook"), QStringLiteral("todoist"), QStringLiteral("待办"),
         }},
        {QStringLiteral("internet"),
         {
             QStringLiteral("chrome"), QStringLiteral("firefox"), QStringLiteral("edge"),
             QStringLiteral("浏览器"), QStringLiteral("browser"),
             QStringLiteral("thunder"), QStringLiteral("迅雷"),
             QStringLiteral("internet download manager"), QStringLiteral("idm"),
             QStringLiteral("motrix"), QStringLiteral("aria2"),
             QStringLiteral("ftp"), QStringLiteral("filezilla"), QStringLiteral("winscp"),
             QStringLiteral("rustdesk"), QStringLiteral("teamviewer"),
             QStringLiteral("anydesk"), QStringLiteral("向日葵"), QStringLiteral("todesk"),
             QStringLiteral("sunlogin"), QStringLiteral("vnc"), QStringLiteral("mstsc"),
             QStringLiteral("远程"), QStringLiteral("zoom"), QStringLiteral("teams"),
             QStringLiteral("discord"), QStringLiteral("telegram"), QStringLiteral("skype"),
             QStringLiteral("微信"), QStringLiteral("wechat"), QStringLiteral("qq"),
             QStringLiteral("dingtalk"), QStringLiteral("钉钉"), QStringLiteral("feishu"),
             QStringLiteral("飞书"), QStringLiteral("lark"), QStringLiteral("wireshark"),
             QStringLiteral("fiddler"), QStringLiteral("charles"),
             QStringLiteral("代理"), QStringLiteral("clash"), QStringLiteral("v2ray"),
             QStringLiteral("mail"), QStringLiteral("localsend"), QStringLiteral("google drive"),
             QStringLiteral("picgo"), QStringLiteral("filez"), QStringLiteral("securefx"),
             QStringLiteral("zerotier"), QStringLiteral("夸克"),
         }},
        {QStringLiteral("utility"),
         {
             QStringLiteral("7-zip"), QStringLiteral("winrar"), QStringLiteral("bandizip"),
             QStringLiteral("压缩"), QStringLiteral("rufus"), QStringLiteral("ventoy"),
             QStringLiteral("diskgenius"), QStringLiteral("分区"),
             QStringLiteral("crystaldiskinfo"), QStringLiteral("aida64"),
             QStringLiteral("cpu-z"), QStringLiteral("gpu-z"), QStringLiteral("hwinfo"),
             QStringLiteral("驱动"), QStringLiteral("driver"),
             QStringLiteral("snipaste"), QStringLiteral("pixpin"), QStringLiteral("截图"),
             QStringLiteral("everything"), QStringLiteral("listary"), QStringLiteral("utools"),
             QStringLiteral("quicklook"), QStringLiteral("ccleaner"),
             QStringLiteral("火绒"), QStringLiteral("杀毒"), QStringLiteral("防火墙"),
             QStringLiteral("管理工具"), QStringLiteral("administrative"),
             QStringLiteral("services"), QStringLiteral("event viewer"),
             QStringLiteral("task scheduler"), QStringLiteral("计算机管理"),
             QStringLiteral("控制面板"), QStringLiteral("系统配置"), QStringLiteral("计算器"),
             QStringLiteral("画图"), QStringLiteral("磁盘"), QStringLiteral("disk"),
             QStringLiteral("sandboxie"), QStringLiteral("沙盘"), QStringLiteral("dism"),
             QStringLiteral("revo"), QStringLiteral("geek"), QStringLiteral("netassist"),
             // Windows 那一整套系统工具。
             // "accessibility" / "administrative" 是按开始菜单的文件夹名匹配的 ——
             // 辅助功能那 5 个（放大镜 / 讲述人 / 屏幕键盘…）和「管理工具」一整夹都是。
             QStringLiteral("accessibility"), QStringLiteral("command prompt"),
             QStringLiteral("character map"), QStringLiteral("computer management"),
             QStringLiteral("registry editor"), QStringLiteral("resource monitor"),
             QStringLiteral("performance monitor"), QStringLiteral("print management"),
             QStringLiteral("system configuration"), QStringLiteral("system information"),
             QStringLiteral("memory diagnostics"), QStringLiteral("recovery"),
             QStringLiteral("iscsi"), QStringLiteral("dfrgui"), QStringLiteral("steps recorder"),
             QStringLiteral("on-screen keyboard"), QStringLiteral("magnify"),
             QStringLiteral("narrator"), QStringLiteral("live captions"),
             QStringLiteral("voice access"), QStringLiteral("auto dark mode"),
             QStringLiteral("scanner"), QStringLiteral("canon"),
             QStringLiteral("codemeter"), QStringLiteral("smart card"),
             QStringLiteral("设备管理器"), QStringLiteral("命令提示符"),
             QStringLiteral("firewall"), QStringLiteral("security configuration"),
             QStringLiteral("task manager"), QStringLiteral("chipset"),
             QStringLiteral("netconfig"), QStringLiteral("网络配置"),
             QStringLiteral("livecaptions"), QStringLiteral("voiceaccess"),
             QStringLiteral("爱思"),
         }},
    };
    return table;
}

} // namespace

namespace AppCategory {

QStringList keys()
{
    return {QStringLiteral("all"), QStringLiteral("dev"), QStringLiteral("media"),
            QStringLiteral("office"), QStringLiteral("internet"), QStringLiteral("utility"),
            QStringLiteral("other")};
}

QStringList labels()
{
    return {QStringLiteral("全部"), QStringLiteral("开发"), QStringLiteral("影音"),
            QStringLiteral("办公"), QStringLiteral("网络"), QStringLiteral("工具"),
            QStringLiteral("其他")};
}

QString label(const QString &key)
{
    const QStringList k = keys();
    const QStringList l = labels();
    const int i = k.indexOf(key);
    return i >= 0 ? l.at(i) : l.first();
}

QString classify(const QString &name, const QString &targetPath)
{
    const QString haystack = (name + QLatin1Char(' ') + targetPath).toLower();

    for (const Rule &rule : rules()) {
        for (const QString &kw : rule.keywords) {
            if (haystack.contains(kw))
                return rule.key;
        }
    }
    return QStringLiteral("other");
}

} // namespace AppCategory
