#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTextStream>
#include <QRegularExpression>
#include <QDebug>
#include <functional>
#include <QDirIterator>
#include <QMap>

// ================== Вспомогательные функции ==================

bool extractHead(const QString &html, QString &headOpenTag, QString &innerHead, int &headEndPos)
{
    int headStart = html.indexOf("<head", 0, Qt::CaseInsensitive);
    if (headStart == -1) {
        qWarning() << "Тег <head> не найден";
        return false;
    }

    int openTagEnd = html.indexOf('>', headStart);
    if (openTagEnd == -1) {
        qWarning() << "Не найден конец открывающего тега <head>";
        return false;
    }

    headOpenTag = html.mid(headStart, openTagEnd - headStart + 1);

    int headClose = html.indexOf("</head", openTagEnd, Qt::CaseInsensitive);
    if (headClose == -1) {
        qWarning() << "Закрывающий тег </head> не найден";
        return false;
    }

    int closeTagEnd = html.indexOf('>', headClose);
    if (closeTagEnd == -1) {
        qWarning() << "Не найден конец закрывающего тега </head>";
        return false;
    }

    innerHead = html.mid(openTagEnd + 1, headClose - openTagEnd - 1);
    headEndPos = closeTagEnd + 1;
    return true;
}

QString removePrintStyles(const QString &innerHead)
{
    QString result;
    int pos = 0;
    const int len = innerHead.length();

    while (pos < len) {
        int styleStart = innerHead.indexOf("<style", pos, Qt::CaseInsensitive);
        if (styleStart == -1) {
            result.append(innerHead.mid(pos));
            break;
        }

        result.append(innerHead.mid(pos, styleStart - pos));

        int openStyleEnd = innerHead.indexOf('>', styleStart);
        if (openStyleEnd == -1) {
            result.append(innerHead.mid(styleStart));
            break;
        }

        int closeStyle = innerHead.indexOf("</style", openStyleEnd, Qt::CaseInsensitive);
        if (closeStyle == -1) {
            result.append(innerHead.mid(styleStart));
            break;
        }

        int closeStyleEnd = innerHead.indexOf('>', closeStyle);
        if (closeStyleEnd == -1) {
            result.append(innerHead.mid(styleStart));
            break;
        }

        QString styleContent = innerHead.mid(openStyleEnd + 1, closeStyle - openStyleEnd - 1);
        if (styleContent.contains("@media print", Qt::CaseInsensitive)) {
            pos = closeStyleEnd + 1;
        } else {
            result.append(innerHead.mid(styleStart, closeStyleEnd - styleStart + 1));
            pos = closeStyleEnd + 1;
        }
    }

    return result;
}

QString extractDoctype(const QString &html)
{
    int doctypeStart = html.indexOf("<!DOCTYPE", 0, Qt::CaseInsensitive);
    if (doctypeStart == -1)
        return QString();

    int doctypeEnd = html.indexOf('>', doctypeStart);
    if (doctypeEnd == -1)
        return QString();

    return html.mid(doctypeStart, doctypeEnd - doctypeStart + 1);
}

// ================== Унифицированный поиск div ==================

bool findDiv(const QString &html, std::function<bool(const QString&)> matcher, QString &divContent)
{
    int pos = 0;
    while (true) {
        int divStart = html.indexOf("<div", pos, Qt::CaseInsensitive);
        if (divStart == -1)
            break;

        int openTagEnd = html.indexOf('>', divStart);
        if (openTagEnd == -1) {
            pos = divStart + 4;
            continue;
        }

        QString openTag = html.mid(divStart, openTagEnd - divStart + 1);
        if (matcher(openTag)) {
            int depth = 1;
            int searchPos = openTagEnd + 1;
            int closePos = -1;

            while (searchPos < html.length()) {
                int nextOpen = html.indexOf("<div", searchPos, Qt::CaseInsensitive);
                int nextClose = html.indexOf("</div", searchPos, Qt::CaseInsensitive);

                if (nextClose == -1)
                    break;

                if (nextOpen != -1 && nextOpen < nextClose) {
                    depth++;
                    searchPos = html.indexOf('>', nextOpen) + 1;
                    if (searchPos <= 0) break;
                } else {
                    depth--;
                    if (depth == 0) {
                        int closeTagEnd = html.indexOf('>', nextClose);
                        if (closeTagEnd != -1) {
                            closePos = closeTagEnd + 1;
                        }
                        break;
                    } else {
                        searchPos = html.indexOf('>', nextClose) + 1;
                        if (searchPos <= 0) break;
                    }
                }
            }

            if (closePos != -1) {
                divContent = html.mid(divStart, closePos - divStart);
                return true;
            } else {
                qWarning() << "Не удалось найти закрывающий тег для div";
                return false;
            }
        }

        pos = openTagEnd + 1;
    }

    return false;
}

bool extractTargetDiv(const QString &html, QString &divContent)
{
    QRegularExpression classRegex("class\\s*=\\s*[\"']([^\"']*)[\"']", QRegularExpression::CaseInsensitiveOption);
    auto matcher = [&](const QString &openTag) -> bool {
        QRegularExpressionMatch match = classRegex.match(openTag);
        if (!match.hasMatch())
            return false;
        QString classValue = match.captured(1);
        QStringList classes = classValue.split(' ', Qt::SkipEmptyParts);
        return classes.contains("paper-1");
    };
    return findDiv(html, matcher, divContent);
}

// ================== Поиск section по классу ==================

bool extractSectionByClass(const QString &html, const QString &classString, QString &sectionContent)
{
    QStringList requiredClasses = classString.split(' ', Qt::SkipEmptyParts);
    QRegularExpression classRegex("class\\s*=\\s*[\"']([^\"']*)[\"']", QRegularExpression::CaseInsensitiveOption);

    int pos = 0;
    while (true) {
        int sectionStart = html.indexOf("<section", pos, Qt::CaseInsensitive);
        if (sectionStart == -1)
            break;

        int openTagEnd = html.indexOf('>', sectionStart);
        if (openTagEnd == -1) {
            pos = sectionStart + 8; // длина "<section"
            continue;
        }

        QString openTag = html.mid(sectionStart, openTagEnd - sectionStart + 1);
        QRegularExpressionMatch match = classRegex.match(openTag);
        bool classesMatch = false;
        if (match.hasMatch()) {
            QString classValue = match.captured(1);
            QStringList classes = classValue.split(' ', Qt::SkipEmptyParts);
            classesMatch = true;
            for (const QString &req : requiredClasses) {
                if (!classes.contains(req)) {
                    classesMatch = false;
                    break;
                }
            }
        }

        if (classesMatch) {
            int depth = 1;
            int searchPos = openTagEnd + 1;
            int closePos = -1;

            while (searchPos < html.length()) {
                int nextOpen = html.indexOf("<section", searchPos, Qt::CaseInsensitive);
                int nextClose = html.indexOf("</section", searchPos, Qt::CaseInsensitive);

                if (nextClose == -1)
                    break;

                if (nextOpen != -1 && nextOpen < nextClose) {
                    depth++;
                    searchPos = html.indexOf('>', nextOpen) + 1;
                    if (searchPos <= 0) break;
                } else {
                    depth--;
                    if (depth == 0) {
                        int closeTagEnd = html.indexOf('>', nextClose);
                        if (closeTagEnd != -1) {
                            closePos = closeTagEnd + 1;
                        }
                        break;
                    } else {
                        searchPos = html.indexOf('>', nextClose) + 1;
                        if (searchPos <= 0) break;
                    }
                }
            }

            if (closePos != -1) {
                sectionContent = html.mid(sectionStart, closePos - sectionStart);
                return true;
            } else {
                qWarning() << "Не удалось найти закрывающий тег для section";
                return false;
            }
        }

        pos = openTagEnd + 1;
    }

    return false;
}

// ================== Новые стили (общие) ==================

const QString newStyle = R"(
<style>
@media print {
    orontin {
        display: block;
        height: 0;
        page-break-before: always;
        break-before: always;
    }

    // * {
    //     page-break-inside: avoid;
    //     break-inside: avoid;
    // }
}
</style>
<style>
    body {
        width: 1220px;
        min-width: 1220px;
        max-width: 1220px;
    }
</style>
)";

// ================== Варианты скриптов ==================

// Вариант 1 (стандартный) — с двумя вызовами
const QString newScriptVariant1 = R"(
<script>
    class Orontin extends HTMLElement { constructor() { super(); } }
    try { customElements.define('orontin', Orontin); } catch (e) {}

    function insertPageBreaksBetweenHeadersInSections(pageHeight = 1685, sectionSelector = 'h2', headerSelector = 'h3, h4') {
        let sections = Array.from(document.querySelectorAll(sectionSelector));

        function processHeaders(headersList) {
            let accumulated = 0;
            for (let i = 0; i < headersList.length - 1; i++) {
                const current = headersList[i];
                const next = headersList[i + 1];

                const currentTop = current.getBoundingClientRect().top + window.scrollY;
                const nextTop = next.getBoundingClientRect().top + window.scrollY;
                const distance = nextTop - currentTop;
                accumulated += distance;

                console.log(`Расстояние между "${current.tagName}" и "${next.tagName}": ${distance}px : ${accumulated}px`);

                if (accumulated >= pageHeight) {
                    const pageBreak = document.createElement('orontin');
                    current.before(pageBreak);
                    accumulated = distance;
                    console.log(`Разрыв между "${current.tagName}" и "${next.tagName}"`);
                }
            }
        }

        const allSubHeaders = Array.from(document.querySelectorAll(headerSelector)).map(header => ({
            element: header,
            top: header.getBoundingClientRect().top + window.scrollY
        }));

        for (let i = 0; i < sections.length; i++) {
            console.log(`-----------------------------------------------------`);

            let startH2 = sections[i];
            let startTop = startH2.getBoundingClientRect().top + window.scrollY;

            let endH2 = sections[i + 1];
            let endTop = endH2 ? endH2.getBoundingClientRect().top + window.scrollY : Infinity;

            let filtered = allSubHeaders
                .filter(item => item.top > startTop && item.top < endTop)
                .map(item => item.element);

            let headersInSection = [startH2, ...filtered, endH2];

            processHeaders(headersInSection);

            console.log(`=====================================================`);
        }
    }

    function insertPageBreaks(headerSelector = 'h1, h2, h3, h4, h5, h6') {
        const headers = Array.from(document.querySelectorAll(headerSelector));
        for (let i = 0; i < headers.length - 1; i++) {
            const current = headers[i];
            const next = headers[i + 1];
            const pageBreak = document.createElement('orontin');
            next.before(pageBreak);
            console.log(`Вставка между  "${current.tagName}" и "${next.tagName}"`);
        }
    }

    window.addEventListener('load', function() {
        document.querySelectorAll('div').forEach(div => {
            if (div.classList.contains('card-menu') || div.classList.contains('card__header') ||
                div.classList.contains('sprite-finik__dice') || div.classList.contains('finik__icon') ||
                div.classList.contains('finik__grid') || div.classList.contains('finik__content') ||
                div.classList.contains('article-body__finik') || div.classList.contains('finik__sign') ||
                div.classList.contains('multiforo-header') || div.classList.contains('inline-menu__item-wrapper') ||
                div.classList.contains('inline-menu')) {
                return;
            }
            div.style.display = 'block';
        });

        insertPageBreaks('h2');
        insertPageBreaksBetweenHeadersInSections(1685, 'h2', 'h3, h4');
    });

    window.addEventListener('beforeprint', () => {
        console.log('Размер страницы перед печатью:', window.innerWidth, 'x', window.innerHeight);
    });
</script>
)";

// Вариант 2 (для групп 3,4,5,6) — с одним вызовом insertPageBreaksBetweenHeadersInSections
const QString newScriptVariant2 = R"(
<script>
    class Orontin extends HTMLElement { constructor() { super(); } }
    try { customElements.define('orontin', Orontin); } catch (e) {}

    /**
     * Вставляет разрывы страниц между заголовками на основе накопленного расстояния.
     * Разрыв ставится ПЕРЕД текущим заголовком, если накопленная сумма (включая расстояние
     * до следующего) достигла или превысила высоту страницы.
     *
     * @param {number} pageHeight - высота страницы в пикселях (по умолчанию 1685)
     * @param {string} headerSelector - селектор всех заголовков для анализа
     */
    function insertPageBreaksByAccumulatedHeight(pageHeight = 1685, headerSelector = 'h1, h2, h3, h4, h5, h6') {
        const headers = Array.from(document.querySelectorAll(headerSelector));
        if (headers.length < 2) return;

        let accumulated = 0; // накопленная сумма расстояний от начала текущей «страницы»

        for (let i = 0; i < headers.length - 1; i++) {
            const current = headers[i];
            const next = headers[i + 1];

            const currentTop = current.getBoundingClientRect().top + window.scrollY;
            const nextTop = next.getBoundingClientRect().top + window.scrollY;
            const distance = nextTop - currentTop;

            accumulated += distance;

            // Если накопленная сумма достигла или превысила высоту страницы И это не первый заголовок,
            // вставляем разрыв ПЕРЕД текущим заголовком и начинаем новую страницу.
            if (accumulated >= pageHeight && i > 0) {
                const pageBreak = document.createElement('orontin');
                current.before(pageBreak);
                console.log(`⚠️ Разрыв перед "${current.tagName}" (накоплено ${accumulated}px >= ${pageHeight}px)`);

                // Сбрасываем накопление, начиная с расстояния от текущего заголовка до следующего
                accumulated = distance;
            } else {
                console.log(`Расстояние между "${current.tagName}" и "${next.tagName}": ${distance}px, накоплено: ${accumulated}px`);
            }
        }
    }

    // Прежняя функция (можно удалить, если не нужна)
    function insertPageBreaks(headerSelector = 'h1, h2, h3, h4, h5, h6') {
        const headers = Array.from(document.querySelectorAll(headerSelector));
        for (let i = 0; i < headers.length - 1; i++) {
            const next = headers[i + 1];
            const pageBreak = document.createElement('orontin');
            next.before(pageBreak);
            console.log(`Вставка между h* и "${next.tagName}"`);
        }
    }

    window.addEventListener('load', function() {
        // Принудительное отображение блоков (без изменений)
        document.querySelectorAll('div').forEach(div => {
            if (div.classList.contains('card-menu') || div.classList.contains('card__header') ||
                div.classList.contains('sprite-finik__dice') || div.classList.contains('finik__icon') ||
                div.classList.contains('finik__grid') || div.classList.contains('finik__content') ||
                div.classList.contains('article-body__finik') || div.classList.contains('finik__sign') ||
                div.classList.contains('multiforo-header') || div.classList.contains('inline-menu__item-wrapper') ||
                div.classList.contains('inline-menu')) {
                return;
            }
            div.style.display = 'block';
        });

        // Дополнительное условие: если встречается h2 с текстом, содержащим "Галерея" (без учёта регистра),
        // превращаем его в h6.
        document.querySelectorAll('h2').forEach(h2 => {
            if (h2.textContent.toLowerCase().includes('галерея')) {
                const h6 = document.createElement('h6');
                // Копируем все атрибуты
                for (let attr of h2.attributes) {
                    h6.setAttribute(attr.name, attr.value);
                }
                // Копируем внутреннее содержимое
                h6.innerHTML = h2.innerHTML;
                // Заменяем h2 на h6
                h2.replaceWith(h6);
                console.log('🔁 Заменён h2 с "Галерея" на h6');
            }
        });

        // Запускаем с настройками, близкими к исходному вызову: все заголовки от h1 до h6
        insertPageBreaksByAccumulatedHeight(1685, 'h1, h2, h3, h4, h5');
    });

    window.addEventListener('beforeprint', () => {
        console.log('Размер страницы перед печатью:', window.innerWidth, 'x', window.innerHeight);
    });
</script>
)";

// ================== Функция обработки группы файлов (с немедленной записью) ==================

bool processGroup(const QStringList &filePaths, const QString &outputFilePath, const QString &scriptToAdd)
{
    if (filePaths.isEmpty())
        return false;

    // Открываем выходной файл сразу для последовательной записи
    QFile outputFile(outputFilePath);
    if (!outputFile.open(QIODevice::WriteOnly | QIODevice::Text)) {
        qWarning() << "Не удалось создать выходной файл:" << outputFilePath;
        return false;
    }
    QTextStream out(&outputFile);

    QStringList successfulFiles;   // пути файлов, успешно обработанных на первом проходе
    bool firstFile = true;
    bool hasAnySuccess = false;

    // ---- Первый проход: записываем DOCTYPE, открывающий <html> и все <head> фрагменты ----
    foreach (const QString &inputPath, filePaths) {
        QFile inputFile(inputPath);
        if (!inputFile.open(QIODevice::ReadOnly | QIODevice::Text)) {
            qWarning() << "Не удалось открыть входной файл:" << inputPath << " - пропускаем";
            continue;
        }
        QTextStream in(&inputFile);
        QString html = in.readAll();
        inputFile.close();

        QString headOpenTag, innerHead;
        int headEndPos;
        if (!extractHead(html, headOpenTag, innerHead, headEndPos)) {
            qWarning() << "Пропуск файла из-за ошибки в head:" << inputPath;
            continue;
        }

        if (firstFile) {
            // Из первого файла берём DOCTYPE и открывающий тег <head>
            QString doctype = extractDoctype(html);
            if (!doctype.isEmpty())
                out << doctype << "\n";
            out << "<html>\n";
            out << headOpenTag << "\n";
            firstFile = false;
        }

        // Удаляем @media print стили и записываем содержимое head текущего файла
        QString modifiedInner = removePrintStyles(innerHead);
        out << modifiedInner << "\n";

        successfulFiles.append(inputPath);
        hasAnySuccess = true;
    }

    // Если ни один файл не дал валидного head, удаляем пустой выходной файл и выходим
    if (!hasAnySuccess) {
        outputFile.remove();
        qWarning() << "Ни один файл в группе не был обработан:" << filePaths;
        return false;
    }

    // Добавляем новые общие стили и закрываем head, открываем body
    out << newStyle << "\n";
    out << "</head>\n";
    out << "<body>\n";

    // ---- Второй проход: записываем содержимое (paper-1 и галерею) каждого успешного файла ----
    foreach (const QString &inputPath, successfulFiles) {
        QFile inputFile(inputPath);
        if (!inputFile.open(QIODevice::ReadOnly | QIODevice::Text)) {
            qWarning() << "Не удалось повторно открыть файл:" << inputPath << " - пропускаем";
            continue;
        }
        QTextStream in(&inputFile);
        QString html = in.readAll();
        inputFile.close();

        // Извлекаем и пишем блок paper-1
        QString divContent;
        if (extractTargetDiv(html, divContent)) {
            out << divContent << "\n";
        } else {
            qDebug() << "Блок paper-1 не найден в файле:" << inputPath;
        }

        // Извлекаем и пишем галерею, если она не пустая
        QString gallerySectionContent;
        if (extractSectionByClass(html, "block block_100 gallery", gallerySectionContent)) {
            if (!gallerySectionContent.contains("На данный момент в галерее нет изображений.")) {
                out << gallerySectionContent << "\n";
            } else {
                qDebug() << "Блок галереи пропущен, так как содержит сообщение об отсутствии изображений:" << inputPath;
            }
        } else {
            qDebug() << "Блок section с классами block block_100 gallery не найден в файле:" << inputPath;
        }
    }

    // Добавляем скрипт и закрывающие теги
    out << scriptToAdd << "\n";
    out << "<h3></body>\n";
    out << "</html>\n";

    outputFile.close();
    qDebug() << "Сохранён объединённый файл:" << outputFilePath;
    return true;
}

// ================== Точка входа ==================

int main(int argc, char *argv[])
{
    QCoreApplication a(argc, argv);

    // Определяем список пар входная_папка -> выходная_папка
    QVector<QPair<QString, QString>> dirPairs = {
        {"D:/workspace/dnd_su__pdf/html_before/1_РАСЫ_И_ПРОИСХОЖДЕНИЯ",  "D:/workspace/dnd_su__pdf/html_after/1_РАСЫ_И_ПРОИСХОЖДЕНИЯ"},
        {"D:/workspace/dnd_su__pdf/html_before/2_ПРЕДЫСТОРИИ",           "D:/workspace/dnd_su__pdf/html_after/2_ПРЕДЫСТОРИИ"},
        {"D:/workspace/dnd_su__pdf/html_before/3_ЧЕРТЫ",                 "D:/workspace/dnd_su__pdf/html_after/3_ЧЕРТЫ"},
        {"D:/workspace/dnd_su__pdf/html_before/4_ЗАКЛИНАНИЯ",            "D:/workspace/dnd_su__pdf/html_after/4_ЗАКЛИНАНИЯ"},
        {"D:/workspace/dnd_su__pdf/html_before/5_БЕСТИАРИЙ",             "D:/workspace/dnd_su__pdf/html_after/5_БЕСТИАРИЙ"},
        {"D:/workspace/dnd_su__pdf/html_before/6_МАГИЧЕСКИЕ_ПРЕДМЕТЫ",   "D:/workspace/dnd_su__pdf/html_after/6_МАГИЧЕСКИЕ_ПРЕДМЕТЫ"},
        {"D:/workspace/dnd_su__pdf/html_before/7_НОВИЧКУ",               "D:/workspace/dnd_su__pdf/html_after/7_НОВИЧКУ"},
        {"D:/workspace/dnd_su__pdf/html_before/8_СТАТЬИ",                "D:/workspace/dnd_su__pdf/html_after/8_СТАТЬИ"}
    };

    int totalSuccess = 0;
    int totalFail = 0;

    for (int i = 0; i < dirPairs.size(); ++i) {
        const auto &pair = dirPairs[i];
        QString inputBaseDir = pair.first;
        QString outputBaseDir = pair.second;

        // Определяем вариант скрипта по имени входной базовой папки
        QString baseFolderName = QFileInfo(inputBaseDir).fileName();
        bool useVariant2 = (baseFolderName == "3_ЧЕРТЫ" ||
                            baseFolderName == "4_ЗАКЛИНАНИЯ" ||
                            baseFolderName == "5_БЕСТИАРИЙ" ||
                            baseFolderName == "6_МАГИЧЕСКИЕ_ПРЕДМЕТЫ");
        const QString &selectedScript = useVariant2 ? newScriptVariant2 : newScriptVariant1;

        qDebug() << "\n=== Обработка базовой пары: вход =" << inputBaseDir << ", выход =" << outputBaseDir
                 << " (скрипт variant" << (useVariant2 ? "2" : "1") << ") ===";

        QDir inputDir(inputBaseDir);
        if (!inputDir.exists()) {
            qCritical() << "Входная папка не существует:" << inputBaseDir << " — пропускаем пару";
            totalFail++;
            continue;
        }

        // Создаём выходную базовую папку, если её нет
        QDir outputBase(outputBaseDir);
        if (!outputBase.exists()) {
            if (!outputBase.mkpath(".")) {
                qCritical() << "Не удалось создать выходную папку:" << outputBaseDir << " — пропускаем пару";
                totalFail++;
                continue;
            }
        }

        // Получаем список поддиректорий первого уровня во входной базовой папке
        QFileInfoList subDirs = inputDir.entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot);
        if (subDirs.isEmpty()) {
            qWarning() << "В папке нет поддиректорий:" << inputBaseDir << " — пропускаем пару";
            totalFail++;
            continue;
        }

        // Для каждой поддиректории собираем все HTML-файлы внутри неё (рекурсивно)
        foreach (const QFileInfo &subDirInfo, subDirs) {
            QString subDirPath = subDirInfo.absoluteFilePath();
            QString subDirName = subDirInfo.fileName();

            qDebug() << "  Обработка поддиректории:" << subDirName;

            // Собираем все HTML-файлы в этой поддиректории (рекурсивно)
            QStringList nameFilters;
            nameFilters << "*.html" << "*.htm";
            QDirIterator it(subDirPath, nameFilters, QDir::Files, QDirIterator::Subdirectories);

            QStringList groupFiles;
            while (it.hasNext()) {
                groupFiles.append(it.next());
            }

            if (groupFiles.isEmpty()) {
                qWarning() << "    В поддиректории и её подпапках нет HTML-файлов:" << subDirPath << " — пропускаем";
                totalFail++;
                continue;
            }

            // Формируем выходной путь: файл с именем поддиректории кладётся непосредственно в выходную базовую папку
            QString outputFilePath = outputBaseDir + "/" + subDirName + ".html";

            // Обрабатываем группу файлов
            if (processGroup(groupFiles, outputFilePath, selectedScript))
                ++totalSuccess;
            else
                ++totalFail;
        }
    }

    qDebug() << "\n=== ИТОГОВЫЙ РЕЗУЛЬТАТ ===";
    qDebug() << "Всего обработано групп:" << totalSuccess << ", ошибок:" << totalFail;

    return 0;
}
