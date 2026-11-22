#include "semantic/models.h"

#include <iterator>

using namespace Qt::StringLiterals;

namespace brulion::semantic {

namespace {

// Brulion's conversion of intfloat/multilingual-e5-small (MIT) to GGUF, 8-bit (docs/SEMANTIC.md
// "The model file": convert_e5_gguf.py at the revision below, then llama-quantize q8_0, both of the
// llama.cpp release 3rdparty/llama/pin.json pins). The download is checked against its SHA-256, so
// the bytes never change whatever the repository does; kRepository / kRevision say where it comes
// from — one constant to change when it moves.
const QString kRepository = u"https://huggingface.co/janusz-bit/multilingual-e5-small-gguf"_s;
const QString kRevision = u"main"_s;
/// The file is not published at kRepository yet (see semanticModelPublished()): true once it is.
constexpr bool kPublished = false;
/// The Hugging Face model it was converted from, at the revision converted.
const QString kSource = u"https://huggingface.co/intfloat/multilingual-e5-small"_s;

/// The mean embedding of the training queries of the search experiments (scripts/semantic/bench:
/// 519 short Polish and English note-search queries of trainC + trainD, export_calib.py), of
/// multilingual-e5-small.
constexpr float kE5SmallQueryMean[] = {
        0.04366163f, -0.01223527f, -0.04429237f, -0.06447336f, 0.07386781f, -0.02355005f, 0.02232212f, 0.02504658f,
        0.01378287f, 0.02268323f, 0.04382301f, 0.0173382f, 0.02844674f, -0.04066191f, -0.02534659f, 0.03318603f,
        0.06451096f, -0.05534625f, -0.04462456f, -0.0494776f, 0.01078748f, -0.02219263f, -0.04164166f, 0.03759766f,
        0.04927512f, 0.02642317f, -0.04000788f, 0.01564677f, 0.03847987f, -0.0745305f, -0.05010533f, -0.05022094f,
        0.0425957f, -0.04783024f, 0.05868731f, 0.03748342f, -0.05191468f, -0.04123649f, 0.05258074f, -0.03903228f,
        -0.03815662f, 0.03795068f, 0.02960067f, 0.0667711f, 0.03171716f, 0.02773304f, -0.05015912f, 0.04715429f,
        -0.0373658f, -0.03478693f, -0.07945694f, 0.06379791f, 0.02467346f, 0.04742847f, 0.06715494f, -0.07125565f,
        -0.05261848f, -0.07825261f, -0.06630027f, 0.03995783f, 0.05992956f, -0.02617016f, 0.002375641f, 0.007553846f,
        0.04596552f, 0.06521624f, 0.03000595f, 0.0274379f, -0.05968544f, -0.04762296f, -0.0465555f, 0.02946518f,
        0.01784291f, -0.02912631f, 0.006064039f, 0.02323053f, 0.0313044f, -0.05137101f, 0.05536338f, -0.04467489f,
        -0.02893744f, -0.04045689f, -0.05713714f, 0.04327965f, -0.06316398f, 0.07254555f, 0.04138562f, -0.06415816f,
        0.04177188f, -0.02334948f, 0.036968f, 0.0508554f, -0.05547161f, -0.05836723f, -0.06481875f, -0.06538309f,
        -0.05151092f, 0.03249346f, 0.04672042f, -0.02687318f, 0.04326335f, -0.03342566f, 0.04423911f, -0.06176968f,
        -0.03054455f, 0.05007346f, 0.04281509f, -0.03762872f, 0.05995132f, -0.05447673f, -0.04531945f, 0.03608523f,
        0.05959715f, 0.03842164f, -0.08633631f, -0.008702538f, -0.026f, -0.0458346f, 0.04039197f, -0.04762036f,
        0.07278249f, -0.03880515f, -0.0535398f, -0.05413762f, -0.04761205f, -0.0272411f, 0.02511213f, 0.02241013f,
        0.02165529f, 0.04234293f, 0.04899485f, 0.02969013f, 0.01829127f, 0.04580425f, 0.04066059f, 0.06689979f,
        -0.0394424f, -0.001462127f, -0.03107815f, -0.04033633f, -0.03269197f, 0.05426508f, -0.04074233f, 0.03719924f,
        0.04722004f, 0.04126921f, 0.0966351f, -0.03550398f, 0.07373972f, -0.02627068f, 0.05330297f, -0.02604968f,
        0.0464415f, 0.02592634f, 0.0534981f, -0.04334096f, -0.06805813f, -0.02870356f, 0.02935153f, 0.04500578f,
        -0.05153685f, -0.05572248f, -0.05886365f, -0.005380035f, -0.04421008f, -0.05699186f, 0.03886292f, 0.0642447f,
        -0.04984359f, -0.06286409f, -0.03299137f, 0.05579142f, -0.02478683f, 0.07107992f, -0.00525878f, 0.06191831f,
        -0.03894377f, 0.07163247f, 0.06408177f, 0.03564188f, -0.02106839f, -0.01485605f, -0.0600855f, -0.05265312f,
        -0.04884071f, -0.05413077f, -0.06836795f, 0.02718991f, 0.01604175f, -0.01957553f, 1.644489e-05f, 0.04092297f,
        -0.02863985f, -0.08308317f, -0.04048271f, 0.0498254f, -0.03509548f, 0.05509462f, 0.04105509f, 0.03894098f,
        -0.008505998f, -0.06072481f, 0.05022698f, 0.04230706f, 0.04728504f, 0.02803322f, -0.06109611f, 0.05148352f,
        -0.037381f, 0.04291124f, 0.04331813f, -0.04756967f, -0.05238175f, 0.05180792f, -0.05577491f, -0.03090188f,
        0.02431172f, 0.0627158f, -0.02296595f, 0.05932933f, 0.06106764f, -0.03400524f, 0.04753077f, -0.06698605f,
        -0.02509899f, 0.03278947f, 0.06126959f, -0.06167451f, -0.04155181f, 0.03924291f, -0.05326414f, -0.03899756f,
        -0.05856583f, -0.06880234f, -0.04998273f, -0.06977104f, -0.01593642f, 0.02864461f, 0.04219913f, -0.05284087f,
        -0.04143976f, -0.02585556f, 0.05369582f, -0.05209235f, 0.04768893f, -0.04509561f, -0.02837203f, 0.03801076f,
        -0.02421531f, 0.03945224f, 0.01972307f, -0.07162189f, -0.0382502f, -0.05155748f, -0.02940014f, 0.03668214f,
        0.04376453f, 0.05653836f, -0.05589145f, 0.02851299f, 0.04661483f, -0.0148017f, 0.0782915f, 0.04994955f,
        0.04764123f, 0.03044895f, -0.06298527f, -0.009159395f, -0.04508916f, -0.04217384f, -0.05782269f, 0.02389986f,
        0.04870759f, -0.0278465f, -0.02948453f, -0.05428011f, 0.0330842f, 0.08870654f, -0.04294295f, -0.03638057f,
        0.05090488f, 0.02622714f, 0.05856187f, 0.05845634f, 0.04781146f, -0.04407724f, -0.008028838f, 0.06251027f,
        -0.0294339f, -0.02608166f, -0.04452183f, -0.06821899f, 0.05663657f, -0.05531254f, 0.05713961f, 0.04899138f,
        0.01783627f, 0.04893452f, -0.02759867f, 0.02897044f, 0.00119189f, -0.06080812f, 0.03187107f, 0.05457135f,
        -0.05951403f, 0.03424279f, -0.01692828f, 0.01983153f, 0.02615821f, 0.04681628f, 0.05041436f, 0.0554102f,
        -0.05247108f, -0.04260911f, 0.05291678f, 0.02648968f, 0.001984589f, 0.02296287f, -0.04655986f, -0.03623772f,
        -0.04154505f, -0.05313318f, -0.01350421f, -0.0498324f, 0.06585849f, 0.01524706f, -0.04328632f, -0.02831735f,
        0.02786267f, 0.009193318f, 0.02311024f, -0.049248f, -0.03056648f, 0.04761154f, -0.03904958f, -0.03372976f,
        -0.03219727f, 0.0491237f, -0.04397999f, -0.03783721f, 0.03156636f, 0.02612338f, -0.04695124f, 0.04991702f,
        -0.01931001f, -0.05107591f, 0.05020534f, -0.03127005f, -0.06338087f, 0.04010051f, 0.03743486f, -0.1150583f,
        0.02313099f, 0.05759555f, -0.03071067f, 0.04478016f, -0.06301752f, -0.0440097f, 0.01678248f, 0.04465972f,
        -0.05793526f, -0.03487694f, 0.02934102f, 0.04226021f, 0.05737697f, 0.0451657f, -0.01990199f, -0.02734984f,
        0.04323112f, -0.02421876f, 0.04453332f, 0.05082029f, -0.04127488f, 0.02539447f, -0.01121921f, -0.05885579f,
        -0.03728696f, 0.04708171f, -0.04150647f, -0.03524271f, 0.06783755f, 0.04779708f, 0.02622998f, 0.03956323f,
};
static_assert(std::size(kE5SmallQueryMean) == 384);

QList<ai::LocalModel> knownModels()
{
    ai::LocalModel e5;
    e5.id = u"multilingual-e5-small"_s;
    e5.name = u"Multilingual E5 Small"_s;
    e5.label = u"E5 Small"_s;
    e5.maker = u"Microsoft"_s;
    e5.format = u"e5"_s;
    const QString file = u"multilingual-e5-small-q8_0.gguf"_s;
    e5.file = {file, QUrl(u"%1/resolve/%2/%3"_s.arg(kRepository, kRevision, file)),
               "3a98dd889ee152d923991c60df2fad21e39f7441a5ee302a0a050d5137993b60", 132440544};
    e5.license = u"MIT"_s;
    e5.licenseUrl = QUrl(u"https://opensource.org/license/mit"_s);
    e5.pageUrl = QUrl(kSource);
    e5.thinks = false;
    return {e5};
}

} // namespace

bool semanticModelPublished()
{
    return kPublished;
}

const QList<ai::LocalModel> &semanticModels()
{
    static const QList<ai::LocalModel> models = knownModels();
    return models;
}

const ai::LocalModel *semanticModel(const QString &id)
{
    const QList<ai::LocalModel> &models = semanticModels();
    if (id.isEmpty())
        return models.isEmpty() ? nullptr : &models.first();
    for (const ai::LocalModel &model : models) {
        if (model.id == id)
            return &model;
    }
    return nullptr;
}

Vector queryMeanOf(const QString &id)
{
    if (id == u"multilingual-e5-small")
        return Vector(std::begin(kE5SmallQueryMean), std::end(kE5SmallQueryMean));
    return {};
}

QString indexFileName(const QString &vaultId, const QString &modelId)
{
    return u"%1-%2.vectors"_s.arg(vaultId, modelId);
}

} // namespace brulion::semantic
