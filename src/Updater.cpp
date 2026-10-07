#include "Updater.h"
#include "Ini.h"
#include "Json.h"
#include "Log.h"
#include "Version.h"
#include <winhttp.h>
#include <cstdio>
#include <vector>

namespace {

std::wstring ExePath() {
  wchar_t path[MAX_PATH];
  GetModuleFileNameW(nullptr, path, MAX_PATH);
  return path;
}

// HTTPS GET (follows GitHub's redirects to its download servers). Returns false on any failure.
bool HttpsGet(const std::wstring& url, std::string& body, std::string& error, const wchar_t* accept) {
  URL_COMPONENTS uc{sizeof(uc)};
  wchar_t host[256], path[2048];
  uc.lpszHostName = host;
  uc.dwHostNameLength = 256;
  uc.lpszUrlPath = path;
  uc.dwUrlPathLength = 2048;
  wchar_t extra[2048];
  uc.lpszExtraInfo = extra;
  uc.dwExtraInfoLength = 2048;
  if (!WinHttpCrackUrl(url.c_str(), 0, 0, &uc)) { error = "bad URL"; return false; }
  std::wstring fullPath = std::wstring(path, uc.dwUrlPathLength) + std::wstring(extra, uc.dwExtraInfoLength);

  HINTERNET s = WinHttpOpen(L"LmuOverlay-Updater", WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY, WINHTTP_NO_PROXY_NAME,
                            WINHTTP_NO_PROXY_BYPASS, 0);
  if (!s) { error = "network unavailable"; return false; }
  WinHttpSetTimeouts(s, 5000, 5000, 10000, 30000);
  bool ok = false;
  HINTERNET c = WinHttpConnect(s, std::wstring(host, uc.dwHostNameLength).c_str(), uc.nPort, 0);
  HINTERNET r = c ? WinHttpOpenRequest(c, L"GET", fullPath.c_str(), nullptr, WINHTTP_NO_REFERER,
                                       WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE)
                  : nullptr;
  if (r) {
    std::wstring headers = std::wstring(L"Accept: ") + accept + L"\r\nX-GitHub-Api-Version: 2022-11-28\r\n";
    if (WinHttpSendRequest(r, headers.c_str(), static_cast<DWORD>(-1L), WINHTTP_NO_REQUEST_DATA, 0, 0, 0) &&
        WinHttpReceiveResponse(r, nullptr)) {
      DWORD status = 0, size = sizeof(status);
      WinHttpQueryHeaders(r, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, WINHTTP_HEADER_NAME_BY_INDEX, &status,
                          &size, WINHTTP_NO_HEADER_INDEX);
      body.clear();
      DWORD avail = 0;
      while (WinHttpQueryDataAvailable(r, &avail) && avail > 0) {
        const size_t at = body.size();
        body.resize(at + avail);
        DWORD read = 0;
        if (!WinHttpReadData(r, body.data() + at, avail, &read)) break;
        body.resize(at + read);
        if (body.size() > 64u * 1024 * 1024) break;
      }
      ok = status == 200;
      if (!ok) error = status == 404 ? "no release found (check the repository name)" : "server answered " + std::to_string(status);
    } else {
      error = "could not reach GitHub";
    }
  } else {
    error = "could not reach GitHub";
  }
  if (r) WinHttpCloseHandle(r);
  if (c) WinHttpCloseHandle(c);
  WinHttpCloseHandle(s);
  return ok;
}

} // namespace

int CompareVersions(const std::string& a, const std::string& b) {
  auto parts = [](const std::string& s) {
    std::vector<int> v;
    size_t i = (!s.empty() && (s[0] == 'v' || s[0] == 'V')) ? 1 : 0;
    while (i < s.size()) {
      v.push_back(std::atoi(s.c_str() + i));
      const size_t dot = s.find('.', i);
      if (dot == std::string::npos) break;
      i = dot + 1;
    }
    return v;
  };
  const std::vector<int> x = parts(a), y = parts(b);
  for (size_t i = 0; i < std::max(x.size(), y.size()); ++i) {
    const int p = i < x.size() ? x[i] : 0, q = i < y.size() ? y[i] : 0;
    if (p != q) return p < q ? -1 : 1;
  }
  return 0;
}

Updater::~Updater() {
  if (thread_.joinable()) thread_.join();
}

void Updater::Set(Phase p, std::string error) {
  std::lock_guard lock(mutex_);
  state_.phase = p;
  state_.error = std::move(error);
}

Updater::State Updater::Get() const {
  std::lock_guard lock(mutex_);
  return state_;
}

void Updater::Check(const std::string& repo) {
  if (repo.empty()) { Set(Phase::Failed, "no repository set"); return; }
  const Phase p = Get().phase;
  if (p == Phase::Checking || p == Phase::Downloading) return;
  if (thread_.joinable()) thread_.join();
  repo_ = repo;
  Set(Phase::Checking);
  thread_ = std::jthread([this] { Run(false); });
}

void Updater::Install() {
  if (Get().phase != Phase::Available) return;
  if (thread_.joinable()) thread_.join();
  Set(Phase::Downloading);
  thread_ = std::jthread([this] { Run(true); });
}

void Updater::Run(bool install) {
  SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_LOWEST);
  std::string body, error;
  if (!install) {
    if (!HttpsGet(L"https://api.github.com/repos/" + Widen(repo_) + L"/releases/latest", body, error,
                  L"application/vnd.github+json")) {
      Set(Phase::Failed, error);
      return;
    }
    Json j;
    if (!Json::Parse(body, j)) { Set(Phase::Failed, "unexpected answer from GitHub"); return; }
    const Json* tag = j.Get("tag_name");
    std::string latest = tag && tag->type == Json::Type::String ? tag->string : "";
    if (!latest.empty() && (latest[0] == 'v' || latest[0] == 'V')) latest.erase(0, 1);
    std::string url;
    if (const Json* assets = j.Get("assets"))
      for (const Json& a : assets->array) {
        const Json* name = a.Get("name");
        const Json* dl = a.Get("browser_download_url");
        if (name && dl && _stricmp(name->string.c_str(), "LmuOverlay.exe") == 0) url = dl->string;
      }
    std::string notes;
    if (const Json* b = j.Get("body"); b && b->type == Json::Type::String) notes = b->string.substr(0, 600);
    std::lock_guard lock(mutex_);
    state_.latest = latest;
    state_.notes = notes;
    assetUrl_ = url;
    if (latest.empty()) { state_.phase = Phase::Failed; state_.error = "the latest release has no version tag"; }
    else if (CompareVersions(latest, LMU_OVERLAY_VERSION) <= 0) state_.phase = Phase::UpToDate;
    else if (url.empty()) { state_.phase = Phase::Failed; state_.error = "the release has no LmuOverlay.exe"; }
    else state_.phase = Phase::Available;
    return;
  }

  // Install: download next to the exe, sanity-check, swap.
  if (!HttpsGet(Widen(assetUrl_), body, error, L"application/octet-stream")) { Set(Phase::Failed, error); return; }
  if (body.size() < 200 * 1024 || body[0] != 'M' || body[1] != 'Z') { Set(Phase::Failed, "download looks broken"); return; }
  const std::wstring exe = ExePath(), fresh = exe + L".new", old = exe + L".old";
  FILE* f = nullptr;
  if (_wfopen_s(&f, fresh.c_str(), L"wb") != 0 || !f) { Set(Phase::Failed, "can't write next to the exe"); return; }
  const bool written = fwrite(body.data(), 1, body.size(), f) == body.size();
  fclose(f);
  if (!written) { DeleteFileW(fresh.c_str()); Set(Phase::Failed, "disk write failed"); return; }
  DeleteFileW(old.c_str());
  if (!MoveFileExW(exe.c_str(), old.c_str(), MOVEFILE_REPLACE_EXISTING)) {
    DeleteFileW(fresh.c_str());
    Set(Phase::Failed, "can't replace the exe (is the folder read-only?)");
    return;
  }
  if (!MoveFileExW(fresh.c_str(), exe.c_str(), MOVEFILE_REPLACE_EXISTING)) {
    MoveFileExW(old.c_str(), exe.c_str(), MOVEFILE_REPLACE_EXISTING); // put the old one back
    Set(Phase::Failed, "can't replace the exe");
    return;
  }
  Logf("update installed: %s -> %s", LMU_OVERLAY_VERSION, Get().latest.c_str());
  Set(Phase::Ready);
}

bool Updater::LaunchNew() const {
  const std::wstring exe = ExePath();
  std::wstring cmd = L"\"" + exe + L"\" --wait-pid " + std::to_wstring(GetCurrentProcessId());
  const std::wstring dir = exe.substr(0, exe.find_last_of(L"\\/"));
  STARTUPINFOW si{sizeof(si)};
  PROCESS_INFORMATION pi{};
  if (!CreateProcessW(exe.c_str(), cmd.data(), nullptr, nullptr, FALSE, 0, nullptr, dir.c_str(), &si, &pi)) return false;
  CloseHandle(pi.hThread);
  CloseHandle(pi.hProcess);
  return true;
}

void Updater::CleanUp() {
  const std::wstring old = ExePath() + L".old";
  DeleteFileW(old.c_str());
}
