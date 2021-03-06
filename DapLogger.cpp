#include "DapLogger.h"

DapLogger::DapLogger(QObject *parent, size_t prefix_width)
    : QObject(parent)
{
    dap_set_log_tag_width(prefix_width);
    qInstallMessageHandler(messageHandler);
}

inline log_level DapLogger::castQtMsgToDap(QtMsgType type) {
    switch (type) {
    case QtDebugMsg:
        return log_level::L_DEBUG;
    case QtInfoMsg:
        return log_level::L_INFO;
    case QtWarningMsg:
        return log_level::L_WARNING;
    case QtCriticalMsg:
        return log_level::L_ERROR;
    case QtFatalMsg: // Qt FatalMsg as L'CRITICAL interrupt the program by abort() function
        return log_level::L_CRITICAL;
    }
    std::runtime_error("Can't cast QtMsg");
    return log_level::L_CRITICAL; // fix compile warning
}

void DapLogger::setLogLevel(log_level ll) {
    set_log_level(ll);
}

bool DapLogger::setLogFile(const QString& filePath) {
    return dap_common_init(filePath.toLatin1().data()) == 0;
}

void DapLogger::messageHandler(QtMsgType type,
                               const QMessageLogContext &ctx,
                               const QString & msg)
{
    if(ctx.file) {
        char prefixBuffer[128];
#if defined(Q_OS_LINUX) || defined(Q_OS_MACOS)
        const char *fileName = strrchr(ctx.file, '/');
#elif defined(Q_OS_WIN)
        const char *fileName = strrchr(ctx.file, '\\');
#else
        #error "Not supported platform"
#endif
        fileName = (fileName == Q_NULLPTR ? ctx.file : fileName + 1);
        strcpy(prefixBuffer, fileName);
        sprintf(strrchr(prefixBuffer, '.'), ":%d", ctx.line);
        _log_it(prefixBuffer, castQtMsgToDap(type), msg.toLatin1().data());
    } else {
        _log_it(NULL, castQtMsgToDap(type), msg.toLatin1().data());
    }
}
