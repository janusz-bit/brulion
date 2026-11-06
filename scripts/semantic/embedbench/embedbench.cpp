// embedbench — speed of an embedding model through llama.cpp on this device: CPU and GPU
// (Vulkan…), the same batching as Brulion's Embedder (src/core/semantic/embedder.cpp), no Qt.
// Built for Android by nix/android/embedbench.nix, for the desktop by its CMakeLists.txt.
//
//   embedbench -m model.gguf [-t threads] [-n passages] [--cpu-only] [--gpu-only]
//
// Prints, per device: model load time, passage throughput (tokens/s, chunks/s), the median and
// 90th percentile latency of a short query, and for GPU runs how close its vectors are to the CPU's.
#include <ggml-backend.h>
#include <llama.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <thread>
#include <vector>

namespace {

using Clock = std::chrono::steady_clock;
double ms(Clock::time_point a, Clock::time_point b)
{
    return std::chrono::duration<double, std::milli>(b - a).count();
}

void quiet(ggml_log_level level, const char *text, void *)
{
    if (level == GGML_LOG_LEVEL_ERROR)
        std::fputs(text, stderr);
}

// Polish note-like text, ~150 tokens per passage.
const char *const kWords[] = {"wymiana", "opon", "zimowych", "w", "serwisie", "na", "Woli", "koszt", "180", "zł",
                              "przegląd", "samochodu", "termin", "październik", "lekarz", "kardiolog", "ciśnienie",
                              "przepis", "szarlotka", "jabłka", "kruche", "ciasto", "ogród", "pomidory", "podlewanie",
                              "urlop", "Lizbona", "bilety", "rezerwacja", "spotkanie", "projekt", "serwer", "wdrożenie",
                              "#praca", "[[2025-03-14]]", "-", "zadanie", "zrobić", "jutro", "ważne"};

std::string passage(int i)
{
    std::string s = "passage: Notatka " + std::to_string(i) + "\n";
    unsigned x = 2166136261u ^ unsigned(i);
    for (int w = 0; w < 100; ++w) {
        x = x * 16777619u + 12345u;
        s += kWords[(x >> 8) % (sizeof(kWords) / sizeof(*kWords))];
        s += (w % 17 == 16) ? "\n" : " ";
    }
    return s;
}

struct Runner {
    llama_model *model = nullptr;
    llama_context *ctx = nullptr;
    const llama_vocab *vocab = nullptr;
    int dim = 0;
    std::string device = "CPU";
    double loadMs = 0;

    bool open(const char *path, ggml_backend_dev_t gpu, int threads)
    {
        auto t0 = Clock::now();
        // CPU runs pass an empty device list: with nullptr llama.cpp still sets up every GPU and may
        // move single operations there (op_offload).
        ggml_backend_dev_t devices[2] = {gpu, nullptr};
        llama_model_params mp = llama_model_default_params();
        mp.n_gpu_layers = gpu ? 999 : 0;
        mp.devices = gpu ? devices : devices + 1;
        model = llama_model_load_from_file(path, mp);
        if (!model)
            return false;
        vocab = llama_model_get_vocab(model);
        dim = llama_model_n_embd(model);
        llama_context_params cp = llama_context_default_params();
        cp.embeddings = true;
        cp.pooling_type = LLAMA_POOLING_TYPE_UNSPECIFIED;
        cp.n_ctx = cp.n_batch = cp.n_ubatch = 1024;
        cp.n_seq_max = 32;
        cp.n_threads = cp.n_threads_batch = threads;
        cp.op_offload = gpu != nullptr;
        ctx = llama_init_from_model(model, cp);
        if (gpu)
            device = std::string(ggml_backend_dev_name(gpu)) + " (" + ggml_backend_dev_description(gpu) + ")";
        loadMs = ms(t0, Clock::now());
        return ctx != nullptr;
    }
    ~Runner()
    {
        if (ctx)
            llama_free(ctx);
        if (model)
            llama_model_free(model);
    }

    std::vector<llama_token> tokenize(const std::string &s) const
    {
        std::vector<llama_token> t(s.size() + 8);
        int n = llama_tokenize(vocab, s.data(), int(s.size()), t.data(), int(t.size()), true, false);
        t.resize(size_t(std::max(n, 0)));
        if (t.size() > 512) {
            llama_token last = t.back();
            t.resize(512);
            t.back() = last;
        }
        return t;
    }

    // Embeds texts in batches of <= 1024 tokens / 32 texts; returns unit vectors.
    double tokenizeMs = 0, encodeMs = 0;

    std::vector<std::vector<float>> embed(const std::vector<std::string> &texts, long *tokens)
    {
        std::vector<std::vector<llama_token>> toks;
        auto tt = Clock::now();
        for (const auto &s : texts)
            toks.push_back(tokenize(s));
        tokenizeMs += ms(tt, Clock::now());
        std::vector<std::vector<float>> out;
        size_t b = 0;
        while (b < toks.size()) {
            size_t e = b, total = 0;
            while (e < toks.size() && e - b < 32 && total + toks[e].size() <= 1024)
                total += toks[e++].size();
            llama_batch batch = llama_batch_init(int(total), 0, 1);
            for (size_t i = b; i < e; ++i) {
                for (size_t p = 0; p < toks[i].size(); ++p) {
                    int j = batch.n_tokens++;
                    batch.token[j] = toks[i][p];
                    batch.pos[j] = llama_pos(p);
                    batch.n_seq_id[j] = 1;
                    batch.seq_id[j][0] = llama_seq_id(i - b);
                    batch.logits[j] = 1;
                }
            }
            if (llama_memory_t mem = llama_get_memory(ctx))
                llama_memory_clear(mem, true);
            auto te = Clock::now();
            if (llama_encode(ctx, batch) != 0) {
                llama_batch_free(batch);
                return {};
            }
            encodeMs += ms(te, Clock::now());
            for (size_t i = b; i < e; ++i) {
                const float *v = llama_get_embeddings_seq(ctx, llama_seq_id(i - b));
                std::vector<float> u(v, v + dim);
                double n = 0;
                for (float x : u)
                    n += double(x) * x;
                for (float &x : u)
                    x = float(x / std::sqrt(n));
                out.push_back(std::move(u));
            }
            llama_batch_free(batch);
            *tokens += long(total);
            b = e;
        }
        return out;
    }
};

std::vector<std::vector<float>> benchmark(const char *path, ggml_backend_dev_t gpu, int threads, int n,
                                          bool *ok)
{
    Runner r;
    *ok = r.open(path, gpu, threads);
    if (!*ok) {
        std::printf("%s: cannot load the model\n", gpu ? ggml_backend_dev_name(gpu) : "CPU");
        return {};
    }
    std::vector<std::string> ps;
    for (int i = 0; i < n; ++i)
        ps.push_back(passage(i));
    long tokens = 0;
    r.embed({ps[0]}, &tokens); // warm-up (shader compilation, buffers)
    tokens = 0;
    r.tokenizeMs = r.encodeMs = 0;
    auto t0 = Clock::now();
    auto vecs = r.embed(ps, &tokens);
    double passMs = ms(t0, Clock::now());
    const double passTok = r.tokenizeMs, passEnc = r.encodeMs;
    r.tokenizeMs = r.encodeMs = 0;
    std::vector<double> lat;
    for (int i = 0; i < 40; ++i) {
        long tk = 0;
        auto q0 = Clock::now();
        r.embed({"query: jak wymieniałem opony zimowe " + std::to_string(i)}, &tk);
        lat.push_back(ms(q0, Clock::now()));
    }
    std::sort(lat.begin(), lat.end());
    std::printf("  time split: passages tokenize %.0f ms + encode %.0f ms; 40 queries tokenize %.0f ms + encode %.0f ms\n",
                passTok, passEnc, r.tokenizeMs, r.encodeMs);
    std::printf("device: %s\n  load %.0f ms | passages: %d chunks, %ld tokens in %.0f ms = %.0f tokens/s, %.1f chunks/s\n"
                "  query: median %.1f ms, p90 %.1f ms\n",
                r.device.c_str(), r.loadMs, n, tokens, passMs, tokens / passMs * 1000, n / passMs * 1000,
                lat[lat.size() / 2], lat[lat.size() * 9 / 10]);
    return vecs;
}

} // namespace

int main(int argc, char **argv)
{
    const char *model = nullptr;
    int threads = int(std::clamp(std::thread::hardware_concurrency() / 2, 1u, 4u)), n = 256;
    bool cpu = true, gpu = true;
    for (int i = 1; i < argc; ++i) {
        if (!std::strcmp(argv[i], "-m") && i + 1 < argc)
            model = argv[++i];
        else if (!std::strcmp(argv[i], "-t") && i + 1 < argc)
            threads = std::atoi(argv[++i]);
        else if (!std::strcmp(argv[i], "-n") && i + 1 < argc)
            n = std::atoi(argv[++i]);
        else if (!std::strcmp(argv[i], "--cpu-only"))
            gpu = false;
        else if (!std::strcmp(argv[i], "--gpu-only"))
            cpu = false;
    }
    if (!model) {
        std::fprintf(stderr, "usage: %s -m model.gguf [-t threads] [-n passages] [--cpu-only|--gpu-only]\n", argv[0]);
        return 2;
    }
    llama_log_set(quiet, nullptr);
    // Static builds (Android) have their backends linked in; a GGML_BACKEND_DL build (nixpkgs)
    // loads them from $GGML_BACKEND_DIR.
    if (const char *dir = std::getenv("GGML_BACKEND_DIR"))
        ggml_backend_load_all_from_path(dir);
    else
        ggml_backend_load_all();
    llama_backend_init();
    std::printf("backends:");
    for (size_t i = 0; i < ggml_backend_dev_count(); ++i)
        std::printf(" [%s: %s]", ggml_backend_dev_name(ggml_backend_dev_get(i)),
                    ggml_backend_dev_description(ggml_backend_dev_get(i)));
    std::printf("\nthreads: %d\n", threads);

    std::vector<std::vector<float>> ref;
    bool ok = false;
    if (cpu)
        ref = benchmark(model, nullptr, threads, n, &ok);
    if (gpu) {
        std::vector<ggml_backend_dev_t> gpus;
        for (auto type : {GGML_BACKEND_DEVICE_TYPE_GPU, GGML_BACKEND_DEVICE_TYPE_IGPU})
            for (size_t i = 0; i < ggml_backend_dev_count(); ++i)
                if (ggml_backend_dev_type(ggml_backend_dev_get(i)) == type)
                    gpus.push_back(ggml_backend_dev_get(i));
        if (gpus.empty())
            std::printf("no GPU backend available\n");
        for (auto g : gpus) {
            auto v = benchmark(model, g, threads, n, &ok);
            if (ok && !ref.empty() && v.size() == ref.size()) {
                double worst = 1;
                for (size_t i = 0; i < v.size(); ++i) {
                    double c = 0;
                    for (size_t k = 0; k < v[i].size(); ++k)
                        c += double(v[i][k]) * ref[i][k];
                    worst = std::min(worst, c);
                }
                std::printf("  cosine to the CPU vectors: min %.5f\n", worst);
            }
        }
    }
    llama_backend_free();
    return 0;
}
