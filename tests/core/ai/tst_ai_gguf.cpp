// A model of the user's own (src/core/ai/gguf.h, ownmodels.h): a GGUF file's header read from
// crafted bytes — a well-formed model, a Polish name, its vocabulary's tags, a projector, an
// adapter —, malformed and truncated files refused with a reason (never a crash or a large
// allocation: counts and lengths beyond the file), the memory a model needs and the device's, and
// the list of own models as the app keeps it.

#include "ai/gguf.h"
#include "ai/ownmodels.h"

#include "ggufwriter.h"

#include <QBuffer>
#include <QFile>
#include <QDir>
#include <QFileInfo>
#include <QTemporaryDir>
#include <QTest>

using namespace Qt::StringLiterals;
using namespace brulion;

namespace {

using Gguf = brulion::test::GgufWriter;

ai::GgufInfo read(const QByteArray &bytes)
{
    QBuffer buffer;
    buffer.setData(bytes);
    buffer.open(QIODevice::ReadOnly);
    return ai::readGguf(&buffer, bytes.size());
}

const QByteArray kQwenTemplate = "{%- if tools %}<|im_start|>system\n# Tools … <tool_call> …{%- endif %}"
                                 "{{- '<|im_start|>' + message.role }}{{- '<think>\\n' }}"
                                 "{%- if enable_thinking is defined and enable_thinking is false %}";

/// A Qwen3 4B as its GGUF has it (the counts small).
Gguf qwen()
{
    Gguf g;
    g.text("general.architecture", "qwen3");
    g.text("general.type", "model");
    g.text("general.name", "Qwen3 4B");
    g.text("general.size_label", "4B");
    g.text("general.license", "apache-2.0");
    g.u32("general.file_type", 15);
    g.u32("qwen3.context_length", 32768);
    g.u32("qwen3.block_count", 36);
    g.u32("qwen3.embedding_length", 2560);
    g.u32("qwen3.attention.head_count", 32);
    g.u32("qwen3.attention.head_count_kv", 8);
    g.u32("qwen3.attention.key_length", 128);
    g.u32("qwen3.attention.value_length", 128);
    g.strings("tokenizer.ggml.tokens", {"!", "hello", "<|im_start|>", "<|im_end|>", "<tool_call>", "</tool_call>",
                                        "<think>", "</think>", "<|endoftext|>"});
    g.i32s("tokenizer.ggml.token_type", {1, 1, 3, 3, 4, 4, 4, 4, 3});
    g.text("tokenizer.chat_template", kQwenTemplate);
    g.tensor("token_embd.weight", {2560, 151936}, 12);
    g.tensor("blk.0.attn_norm.weight", {2560}, 0, 1024);
    return g;
}

} // namespace

class TestAiGguf : public QObject {
    Q_OBJECT

private Q_SLOTS:
    void aModelsHeader()
    {
        const Gguf g = qwen();
        const QByteArray bytes = g.bytes();
        const ai::GgufInfo info = read(bytes);
        QVERIFY2(info.ok, qPrintable(info.error));
        QCOMPARE(info.version, 3u);
        QCOMPARE(info.architecture, u"qwen3"_s);
        QCOMPARE(info.type, u"model"_s);
        QCOMPARE(info.name, u"Qwen3 4B"_s);
        QCOMPARE(info.license, u"apache-2.0"_s);
        QCOMPARE(info.fileType, 15);
        QCOMPARE(info.contextLength, 32768LL);
        QCOMPARE(info.blockCount, 36);
        QCOMPARE(info.embeddingLength, 2560);
        QCOMPARE(info.headCount, 32);
        QCOMPARE(info.kvHeads, 8LL * 36);
        QCOMPARE(info.keyLength, 128);
        QCOMPARE(info.vocabularySize, 9LL);
        QCOMPARE(info.tags, (QStringList{u"<|im_start|>"_s, u"<|im_end|>"_s, u"<tool_call>"_s, u"</tool_call>"_s,
                                         u"<think>"_s, u"</think>"_s}));
        QCOMPARE(info.chatTemplate, QString::fromUtf8(kQwenTemplate));
        QCOMPARE(info.tensorCount, 2LL);
        QCOMPARE(info.parameters, 2560LL * 151936 + 2560);
        QCOMPARE(info.fileBytes, qint64(bytes.size()));
        QCOMPARE(ai::quantizationName(info), u"Q4_K_M"_s);
        QCOMPARE(ai::sizeName(info), u"4B"_s);
        // The context's cells: 36 layers × 8 heads × (128 + 128) × 2 bytes a token.
        QCOMPARE(ai::memoryNeeded(info, 16384),
                 qint64(bytes.size()) + 36LL * 8 * 256 * 2 * 16384 + 300LL * 1024 * 1024);
    }

    void namesAreTheFilesOwn()
    {
        // A Polish model's name as its maker wrote it; no size label: the parameters counted.
        Gguf g;
        g.text("general.architecture", "llama");
        g.text("general.name", "Bielik — polski model, zażółć gęślą jaźń");
        g.text("tokenizer.chat_template", "<|im_start|>{{ message['role'] }}");
        g.tensor("token_embd.weight", {1000, 1500}, 2);
        const ai::GgufInfo info = read(g.bytes());
        QVERIFY2(info.ok, qPrintable(info.error));
        QCOMPARE(info.name, u"Bielik — polski model, zażółć gęślą jaźń"_s);
        QCOMPARE(info.fileType, -1);
        QCOMPARE(ai::quantizationName(info), u"Q4_0"_s); // (the type most of it is in)
        QCOMPARE(ai::sizeName(info), u"2M"_s);
        // Head counts given per layer (a hybrid model: 0 where a layer has no attention).
        Gguf hybrid;
        hybrid.text("general.architecture", "lfm3");
        hybrid.u32("lfm3.block_count", 4);
        hybrid.u32("lfm3.embedding_length", 2048);
        hybrid.i32s("lfm3.attention.head_count", {32, 32, 32, 32});
        hybrid.i32s("lfm3.attention.head_count_kv", {0, 8, 0, 8});
        hybrid.tensor("token_embd.weight", {2048, 65536}, 2);
        const ai::GgufInfo lfm = read(hybrid.bytes());
        QVERIFY2(lfm.ok, qPrintable(lfm.error));
        QCOMPARE(lfm.kvHeads, 16LL);
        QCOMPARE(lfm.headCount, 32);
        QCOMPARE(ai::memoryNeeded(lfm, 1000), lfm.fileBytes + 16LL * (64 + 64) * 2 * 1000 + 300LL * 1024 * 1024);
    }

    void malformedFilesAreRefused_data()
    {
        QTest::addColumn<QByteArray>("bytes");
        QTest::addColumn<QString>("error");
        const Gguf good = qwen();
        QTest::newRow("empty") << QByteArray() << u"not a GGUF"_s;
        QTest::newRow("another file") << QByteArray("PK\x03\x04 a zip") << u"not a GGUF"_s;
        QTest::newRow("magic only") << QByteArray("GGUF") << u"ends early"_s;
        Gguf v1 = good;
        v1.version = 1;
        QTest::newRow("version 1") << v1.bytes() << u"first version"_s;
        Gguf v9 = good;
        v9.version = 9;
        QTest::newRow("version 9") << v9.bytes() << u"unknown version"_s;
        Gguf big = good;
        big.version = 0x03000000;
        QTest::newRow("big-endian") << big.bytes() << u"big-endian"_s;
        QTest::newRow("huge tensor count") << good.bytes(4096, quint64(1) << 40) << u"claims more"_s;
        QTest::newRow("huge entry count") << good.bytes(4096, ~quint64(0), quint64(1) << 33) << u"claims more"_s;
        QTest::newRow("more entries than there are") << good.bytes(0, ~quint64(0), 40) << u""_s;

        Gguf longText;
        longText.raw("general.name", 8, QByteArray::fromHex("ffffffffffff0000") + "abc");
        QTest::newRow("a text past the end") << longText.bytes(16) << u"runs past"_s;
        Gguf longList;
        longList.raw("tokenizer.ggml.tokens", 9,
                     QByteArray::fromHex("08000000") + QByteArray::fromHex("00000000ffffff00"));
        QTest::newRow("a list past the end") << longList.bytes(16) << u"runs past"_s;
        Gguf numbers;
        numbers.raw("x.scores", 9, QByteArray::fromHex("06000000") + QByteArray::fromHex("0000000000ffff00"));
        QTest::newRow("numbers past the end") << numbers.bytes(16) << u"runs past"_s;
        Gguf unknown;
        unknown.raw("general.weird", 13, QByteArray(8, '\0'));
        QTest::newRow("an unknown type") << unknown.bytes() << u"unknown type"_s;
        Gguf deep;
        QByteArray nested;
        for (int i = 0; i < 6; ++i) {
            Gguf::number(&nested, 9, 4);
            Gguf::number(&nested, 1, 8);
        }
        deep.raw("general.nested", 9, nested + QByteArray(64, '\0'));
        QTest::newRow("lists nested deep") << deep.bytes() << u"too deeply"_s;
        Gguf dims = good;
        dims.tensor("blk.1.weird", {2, 2, 2, 2, 2}, 0);
        QTest::newRow("five dimensions") << dims.bytes() << u"5 dimensions"_s;
        Gguf zero = good;
        zero.tensor("blk.1.empty", {0, 16}, 0);
        QTest::newRow("a dimension of 0") << zero.bytes() << u"impossible size"_s;
        Gguf overflow = good;
        overflow.tensor("blk.1.huge", {quint64(1) << 40, quint64(1) << 40}, 0);
        QTest::newRow("too many elements") << overflow.bytes() << u"impossible size"_s;
        Gguf beyond = good;
        beyond.tensor("blk.1.far", {16}, 0, quint64(1) << 40);
        QTest::newRow("a tensor past the end") << beyond.bytes() << u"past its end"_s;
    }
    void malformedFilesAreRefused()
    {
        QFETCH(QByteArray, bytes);
        QFETCH(QString, error);
        const ai::GgufInfo info = read(bytes);
        QVERIFY(!info.ok);
        QVERIFY2(!info.error.isEmpty(), "a reason");
        QVERIFY2(info.error.contains(error), qPrintable(info.error));
    }

    void everyCutOffHeaderIsRefused()
    {
        // A download that broke off, a copy cut short: every length short of the header fails
        // with a reason.
        const Gguf g = qwen();
        const QByteArray bytes = g.bytes(64);
        for (qsizetype n = 0; n < g.headerSize(); n += 7) {
            const ai::GgufInfo info = read(bytes.left(n));
            QVERIFY2(!info.ok, qPrintable(u"%1 bytes read as a model"_s.arg(n)));
            QVERIFY(!info.error.isEmpty());
        }
    }

    void longTextsAreSkipped()
    {
        // A chat template of more than a megabyte (no model has one): not kept, the rest read on.
        Gguf g;
        g.text("general.architecture", "qwen3");
        g.text("tokenizer.chat_template", QByteArray(ai::MaxTemplateBytes + 10, 'x'));
        g.text("general.name", "Po szablonie");
        g.tensor("token_embd.weight", {16}, 0);
        const ai::GgufInfo info = read(g.bytes());
        QVERIFY2(info.ok, qPrintable(info.error));
        QVERIFY(info.chatTemplate.isEmpty());
        QCOMPARE(info.name, u"Po szablonie"_s);
    }

    void aFileOnDisk()
    {
        QTemporaryDir dir;
        const QString path = dir.filePath(u"Mój model ąę.gguf"_s);
        QFile file(path);
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write(qwen().bytes());
        file.close();
        const ai::GgufInfo info = ai::readGgufFile(path);
        QVERIFY2(info.ok, qPrintable(info.error));
        QCOMPARE(info.fileBytes, QFileInfo(path).size());
        const ai::GgufInfo none = ai::readGgufFile(dir.filePath(u"brak.gguf"_s));
        QVERIFY(!none.ok);
        QCOMPARE(none.error, u"there is no such file"_s);
    }

    void aFileAsAModelOfTheUsersOwn()
    {
        const ai::GgufInfo info = read(qwen().bytes());
        QString why;
        const std::optional<ai::OwnModel> own = ai::ownModelFrom(u"/home/ja/Modele/Qwen3-4B-Q4_K_M.gguf"_s, info, &why);
        QVERIFY2(own, qPrintable(why));
        QVERIFY(ai::isOwnModelId(own->id));
        QCOMPARE(own->id, ai::ownModelId(u"/home/ja/Modele/Qwen3-4B-Q4_K_M.gguf"_s));
        QCOMPARE(own->name, u"Qwen3 4B"_s);
        QCOMPARE(own->format, u"chatml+think"_s);
        QCOMPARE(own->quantization, u"Q4_K_M"_s);
        QCOMPARE(own->sizeLabel, u"4B"_s);
        QCOMPARE(own->contextLength, 32768);
        QCOMPARE(own->parameters, info.parameters);
        // A name of the user's own goes before the file's.
        QCOMPARE(ai::ownModelFrom(u"/m.gguf"_s, info, &why, u"  Mój   Qwen "_s)->name, u"Mój Qwen"_s);

        const ai::LocalModel model = ai::localModelOf(*own);
        QVERIFY(model.own);
        // (The path made absolute: on Windows with the current drive in front.)
        QCOMPARE(model.file.path, QDir::cleanPath(QFileInfo(u"/home/ja/Modele/Qwen3-4B-Q4_K_M.gguf"_s).absoluteFilePath()));
        QCOMPARE(model.file.name, u"Qwen3-4B-Q4_K_M.gguf"_s);
        QVERIFY(model.thinks);
        QVERIFY(!model.trained);
        QVERIFY(!model.sees());
        QVERIFY(ai::isSmall(model)); // (4 billion parameters)
        QCOMPARE(model.trainedContext, 32768);
    }

    void filesThatAreNoModelOfTheirOwn_data()
    {
        QTest::addColumn<QByteArray>("bytes");
        QTest::addColumn<QString>("error");
        Gguf adapter;
        adapter.text("general.architecture", "lfm3");
        adapter.text("general.type", "adapter");
        adapter.tensor("blk.0.attn_q.weight.lora_a", {16, 2048}, 1);
        QTest::newRow("an adapter") << adapter.bytes() << u"LoRA adapter"_s;
        Gguf projector;
        projector.text("general.architecture", "clip");
        projector.flag("clip.has_vision_encoder", true);
        projector.tensor("v.patch_embd.weight", {14, 14, 3, 1024}, 1);
        QTest::newRow("a projector") << projector.bytes() << u"vision projector"_s;
        Gguf embedding;
        embedding.text("general.architecture", "bert");
        embedding.tensor("token_embd.weight", {384, 30000}, 1);
        QTest::newRow("an embedding model") << embedding.bytes() << u"embeds"_s;
        Gguf nameless;
        nameless.text("general.name", "Bez architektury");
        nameless.tensor("token_embd.weight", {16}, 0);
        QTest::newRow("no architecture") << nameless.bytes() << u"which kind"_s;
        Gguf empty;
        empty.text("general.architecture", "llama");
        empty.text("tokenizer.chat_template", "<|start_header_id|><|end_header_id|>");
        QTest::newRow("no weights") << empty.bytes() << u"no weights"_s;
        Gguf gemma4;
        gemma4.text("general.architecture", "gemma4");
        gemma4.text("tokenizer.chat_template", "{{ '<|turn>' + role }}");
        gemma4.tensor("token_embd.weight", {16}, 0);
        QTest::newRow("a format not written") << gemma4.bytes() << u"none Brulion writes"_s;
        Gguf plain;
        plain.text("general.architecture", "gpt2");
        plain.strings("tokenizer.ggml.tokens", {"a", "b"});
        plain.tensor("token_embd.weight", {16}, 0);
        QTest::newRow("no template, no tags") << plain.bytes() << u"no chat template"_s;
        QTest::newRow("no GGUF") << QByteArray("hello") << u"not a GGUF"_s;
    }
    void filesThatAreNoModelOfTheirOwn()
    {
        QFETCH(QByteArray, bytes);
        QFETCH(QString, error);
        QString why;
        QVERIFY(!ai::ownModelFrom(u"/m.gguf"_s, read(bytes), &why));
        QVERIFY2(why.contains(error), qPrintable(why));
    }

    void aVocabularyTellsTheFormat()
    {
        // No template: ChatML's tags in its vocabulary.
        Gguf g;
        g.text("general.architecture", "qwen2");
        g.strings("tokenizer.ggml.tokens", {"a", "<|im_start|>", "<|im_end|>"});
        g.tensor("token_embd.weight", {16}, 0);
        QString why;
        const std::optional<ai::OwnModel> own = ai::ownModelFrom(u"/m.gguf"_s, read(g.bytes()), &why);
        QVERIFY2(own, qPrintable(why));
        QCOMPARE(own->format, u"chatml"_s);
    }

    void aProjector()
    {
        Gguf projector;
        projector.text("general.architecture", "clip");
        projector.flag("clip.has_vision_encoder", true);
        projector.tensor("v.patch_embd.weight", {14, 14, 3, 1024}, 1);
        QString why;
        QVERIFY2(ai::isProjector(read(projector.bytes()), &why), qPrintable(why));
        Gguf sound;
        sound.text("general.architecture", "clip");
        sound.flag("clip.has_vision_encoder", false);
        sound.tensor("a.conv1d.weight", {16}, 1);
        QVERIFY(!ai::isProjector(read(sound.bytes()), &why));
        QVERIFY2(why.contains(u"no pictures"_s), qPrintable(why));
        QVERIFY(!ai::isProjector(read(qwen().bytes()), &why));
        QVERIFY2(why.contains(u"model of its own"_s), qPrintable(why));
    }

    void theListOfOwnModels()
    {
        ai::OwnModel a;
        a.id = ai::ownModelId(u"/home/ja/Modele/żółw.gguf"_s);
        a.name = u"Żółw 3B"_s;
        a.path = u"/home/ja/Modele/żółw.gguf"_s;
        a.format = u"llama3"_s;
        a.bytes = 2100000000;
        a.parameters = 3200000000;
        a.contextLength = 131072;
        a.architecture = u"llama"_s;
        a.quantization = u"Q4_K_M"_s;
        a.sizeLabel = u"3B"_s;
        a.memory = 3900000000;
        ai::OwnModel b = a;
        b.id = ai::ownModelId(u"/data/models/own/gemma.gguf"_s);
        b.name = u"Gemma z obrazkami"_s;
        b.path = u"/data/models/own/gemma.gguf"_s;
        b.projector = u"/data/models/own/mmproj-gemma.gguf"_s;
        b.projectorBytes = 800000000;
        b.copied = true;
        b.format = u"gemma"_s;
        const QList<ai::OwnModel> list{a, b};
        QCOMPARE(ai::readOwnModels(ai::writeOwnModels(list)), list);

        const ai::LocalModel seeing = ai::localModelOf(b);
        QVERIFY(seeing.sees());
        QCOMPARE(seeing.projector.path, b.projector);
        QCOMPARE(seeing.files().size(), 2);

        // What another version (or a hand) left there: kept only when it is one.
        const QByteArray json = R"({"version": 1, "models": [
            {"id": "own-0123456789ab", "name": "Dobry", "path": "/m/dobry.gguf", "format": "chatml"},
            {"id": "own-0123456789ab", "name": "Ten sam znowu", "path": "/m/inny.gguf", "format": "chatml"},
            {"id": "lfm3-1.7b", "name": "Nie własny", "path": "/m/x.gguf", "format": "lfm3"},
            {"id": "own-ffffffffffff", "name": "Bez ścieżki", "format": "chatml"},
            {"id": "own-aaaaaaaaaaaa", "name": "Nieznany format", "path": "/m/y.gguf", "format": "gemma4"},
            {"id": "own-bbbbbbbbbbbb", "path": "/m/bez nazwy.gguf", "format": "phi3"},
            "nie obiekt", 42]})";
        const QList<ai::OwnModel> read = ai::readOwnModels(json);
        QCOMPARE(read.size(), 2);
        QCOMPARE(read.at(0).name, u"Dobry"_s);
        QCOMPARE(read.at(1).name, u"bez nazwy"_s); // (its file's name)
        QVERIFY(ai::readOwnModels("nie JSON {").isEmpty());
        QVERIFY(ai::readOwnModels(QByteArray()).isEmpty());
    }

    void ids()
    {
        QVERIFY(ai::isOwnModelId(u"own-0123456789ab"_s));
        QVERIFY(!ai::isOwnModelId(u"own-0123456789AB"_s));
        QVERIFY(!ai::isOwnModelId(u"own-0123"_s));
        QVERIFY(!ai::isOwnModelId(u"lfm3-1.7b"_s));
        QVERIFY(!ai::isOwnModelId(QString()));
        QCOMPARE(ai::ownModelId(u"/a/b.gguf"_s), ai::ownModelId(u"/a/./b.gguf"_s));
        QVERIFY(ai::ownModelId(u"/a/b.gguf"_s) != ai::ownModelId(u"/a/c.gguf"_s));
    }

    void theDevicesMemory_data()
    {
        QTest::addColumn<QByteArray>("meminfo");
        QTest::addColumn<qint64>("bytes");
        QTest::newRow("a Raspberry Pi") << QByteArray("MemTotal:        3775512 kB\nMemFree:          338000 kB\n")
                                        << 3775512LL * 1024;
        QTest::newRow("later in the file") << QByteArray("MemFree: 1 kB\nMemTotal: 16 MB\n") << 16LL * 1024 * 1024;
        QTest::newRow("none") << QByteArray("MemFree: 1 kB\n") << 0LL;
        QTest::newRow("not a number") << QByteArray("MemTotal: dużo kB\n") << 0LL;
        QTest::newRow("an unknown unit") << QByteArray("MemTotal: 5 GB\n") << 0LL;
        QTest::newRow("empty") << QByteArray() << 0LL;
    }
    void theDevicesMemory()
    {
        QFETCH(QByteArray, meminfo);
        QFETCH(qint64, bytes);
        QCOMPARE(ai::memTotalFrom(meminfo), bytes);
    }
};

QTEST_GUILESS_MAIN(TestAiGguf)
#include "tst_ai_gguf.moc"
