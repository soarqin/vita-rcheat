#define _CRT_SECURE_NO_WARNINGS

#include "gui.h"

#include "lang.h"
#include "net.h"
#include "command.h"
#include "handler.h"
#include "../version.h"

#include "imgui.h"
#include "imgui_impl_glfw_gl3.h"

#include "yaml-cpp/yaml.h"

#ifdef _WIN32
#include "resource.h"

#include <windows.h>
#include <Shlwapi.h>
#include <ole2.h>
#include <oleauto.h>
#include <mlang.h>
#endif
#include <GL/gl3w.h>
#include <GLFW/glfw3.h>
#ifdef _WIN32
#define GLFW_EXPOSE_NATIVE_WIN32
#include <GLFW/glfw3native.h>
#endif
#include <cstdio>
#include <cstdlib>
#include <cctype>
#include <fstream>
#include <stdexcept>

enum:int {
    WIN_WIDTH = 640,
    WIN_HEIGHT = 640,
};

static void glfwErrorCallback(int error, const char* description) {
    fprintf(stderr, "Error %d: %s\n", error, description);
}

inline const char *getGradeName(int grade) {
    switch (grade) {
        case 1: return LS(TROPHY_GRADE_PLATINUM);
        case 2: return LS(TROPHY_GRADE_GOLD);
        case 3: return LS(TROPHY_GRADE_SILVER);
        case 4: return LS(TROPHY_GRADE_BRONZE);
        default: return LS(TROPHY_GRADE_UNKNOWN);
    }
}

inline const char *getTypeName(uint8_t type) {
    switch (type) {
        case Command::st_autoint: return LS(AUTOINT);
        case Command::st_autouint: return LS(AUTOUINT);
        case Command::st_i32: return LS(INT32);
        case Command::st_u32: return LS(UINT32);
        case Command::st_i16: return LS(INT16);
        case Command::st_u16: return LS(UINT16);
        case Command::st_i8: return LS(INT8);
        case Command::st_u8: return LS(UINT8);
        case Command::st_i64: return LS(INT64);
        case Command::st_u64: return LS(UINT64);
        case Command::st_float: return LS(FLOAT);
        case Command::st_double: return LS(DOUBLE);
        default: return "";
    }
}

Gui::Gui() {
    std::vector<std::string> langs;
#ifdef _WIN32
    wchar_t localname[256];
    char lname[256] = "";
    LCID lcid = GetUserDefaultLCID();

    typedef int (WINAPI *fnLCIDToLocaleName)(LCID Locale, LPWSTR  lpName, int cchName, DWORD dwFlags);
    fnLCIDToLocaleName pfnLCIDToLocaleName = (fnLCIDToLocaleName)::GetProcAddress(GetModuleHandleA("Kernel32"), "LCIDToLocaleName");
    if (pfnLCIDToLocaleName) {
        pfnLCIDToLocaleName(lcid, localname, 256, 0);
        WideCharToMultiByte(CP_ACP, 0, localname, 256, lname, 256, NULL, NULL);
    } else {
        HRESULT hr = CoInitialize(NULL);
        if (SUCCEEDED(hr)) {
            IMultiLanguage * pml;
            hr = CoCreateInstance(CLSID_CMultiLanguage, NULL,
                CLSCTX_ALL,
                IID_IMultiLanguage, (void**)&pml);
            if (SUCCEEDED(hr)) {
                // Let's convert US-English to an RFC 1766 string
                BSTR bs;
                hr = pml->GetRfc1766FromLcid(lcid, &bs);
                if (SUCCEEDED(hr)) {
                    WideCharToMultiByte(CP_ACP, 0, bs, lstrlenW(bs) + 1, lname, 256, NULL, NULL);
                    SysFreeString(bs);
                }
                pml->Release();
            }
            CoUninitialize();
        }
    }
    for (auto &p: lname) {
        if (p == 0) break;
        p = std::tolower(p);
    }
    if (lname[0] != 0) g_lang.setLanguageByCode(lname);
#else
    char *langname = getenv("LANG");
    if (langname == NULL) langname = getenv("LC_ALL");
    if (langname != NULL) {
        char lname[256];
        char *pname = lname;
        size_t len = strlen(langname);
        for (size_t i = 0; i < len; ++i) {
            lname[i] = std::tolower(langname[i]);
        }
        lname[len] = 0;
        g_lang.setLanguageByCode(lname);
    }
#endif
    UdpClient::init();
    client_ = new UdpClient;
    client_->setOnConnected([&](const char *addr) {
        strncpy(ip_, addr, 256);
    });
    cmd_ = new Command(*client_);
    handler_ = new Handler(*this);

    loadData();

    client_->setOnRecv(std::bind(&Handler::process, handler_, std::placeholders::_1, std::placeholders::_2, std::placeholders::_3));

    glfwSetErrorCallback(glfwErrorCallback);
    if (!glfwInit())
        throw std::runtime_error("Unable to initialize GLFW");
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 2);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
#if __APPLE__
    glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GL_TRUE);
#endif
    glfwWindowHint(GLFW_RESIZABLE, 0);
    window_ = glfwCreateWindow(WIN_WIDTH, WIN_HEIGHT, "", NULL, NULL);
#ifdef _WIN32
    HICON hIcon = LoadIcon(GetModuleHandle(NULL), MAKEINTRESOURCE(IDI_MAINICON));
    SendMessage(glfwGetWin32Window(window_), WM_SETICON, ICON_BIG, (LPARAM)hIcon);
    SendMessage(glfwGetWin32Window(window_), WM_SETICON, ICON_SMALL, (LPARAM)hIcon);
#endif
    glfwMakeContextCurrent(window_);
    glfwSwapInterval(1);
    gl3wInit2(glfwGetProcAddress);

    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();

    ImGui_ImplGlfwGL3_Init(window_, true);

    ImGui::StyleColorsDark();
    auto &style = ImGui::GetStyle();
    style.WindowRounding = 0.f;
    style.ItemSpacing = ImVec2(5.f, 5.f);

    io.IniFilename = NULL;
    reloadFonts();
}

Gui::~Gui() {
    saveData();
    delete handler_;
    delete cmd_;
    delete client_;

    ImGui_ImplGlfwGL3_Shutdown();
    glfwDestroyWindow(window_);
    ImGui::DestroyContext();
    glfwTerminate();

    UdpClient::finish();
}

int Gui::run() {
    while (!glfwWindowShouldClose(window_)) {
        if (reloadLang_) {
            reloadLang_ = false;
            reloadFonts();
        }

        client_->process();
        glfwPollEvents();

        glfwGetFramebufferSize(window_, &dispWidth_, &dispHeight_);

        ImGui_ImplGlfwGL3_NewFrame();
#ifndef NDEBUG
        char title[256];
        snprintf(title, 256, "%s v" VERSION_STR " - %.1f FPS", LS(WINDOW_TITLE), ImGui::GetIO().Framerate);
        glfwSetWindowTitle(window_, title);
#endif

        if (ImGui::Begin("", NULL, ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize)) {
            ImGui::SetWindowPos(ImVec2(10.f, 10.f));
            ImGui::SetWindowSize(ImVec2(dispWidth_ - 20.f, dispHeight_ - 20.f));
            connectPanel();
            if (client_->isConnected()) {
                tabPanel();
                switch (tabIndex_) {
                    case 0:
                        searchPanel();
                        searchPopup();
                        break;
                    case 1:
                        memoryPanel();
                        memoryPopup();
                        break;
                    case 2:
                        tablePanel();
                        tablePopup();
                        break;
                    case 3:
                        trophyPanel();
                        break;
                }
            }
            ImGui::End();
        }

        glViewport(0, 0, dispWidth_, dispHeight_);
        glClearColor(0.f, 0.f, 0.f, 1.f);
        glClear(GL_COLOR_BUFFER_BIT);
        ImGui::Render();
        ImGui_ImplGlfwGL3_RenderDrawData(ImGui::GetDrawData());
        glfwSwapBuffers(window_);
    }

    return 0;
}

void Gui::searchResultStart(uint8_t type) {
    searchResults_.clear();
    searchStatus_ = 1;
    searchResultType_ = type;
}

void Gui::searchResult(const SearchVal *vals, int count) {
    for (int i = 0; i < count; ++i) {
        char hex[16], value[64];
        snprintf(hex, 16, "%08X", vals[i].addr);
        cmd_->formatTypeData(value, searchResultType_, &vals[i].val);
        MemoryItem sr = {vals[i].addr, searchResultType_, hex, value};
        searchResults_.push_back(sr);
    }
}

void Gui::searchEnd(int ret) {
    if (ret == 0 && searchResults_.empty())
        searchStatus_ = 0;
    else
        searchStatus_ = 2 + ret;
}

void Gui::trophyList(int id, int grade, bool hidden, bool unlocked, const char *name, const char *desc) {
    if (id >= (int)trophies_.size()) trophies_.resize(id + 1);
    TrophyInfo &ti = trophies_[id];
    ti.id = id;
    ti.grade = grade;
    ti.hidden = hidden;
    ti.unlocked = unlocked;
    ti.name = name;
    ti.desc = desc;
    if (ti.grade == 1) trophyPlat_ = id;
}

void Gui::trophyListEnd() {
    trophyStatus_ = 0;
}

void Gui::trophyListErr() {
    trophyStatus_ = 2;
}

void Gui::trophyUnlocked(int idx, int platidx) {
    if (idx >= 0 && idx < (int)trophies_.size()) {
        trophies_[idx].unlocked = true;
    }
    if (platidx >= 0) {
        trophies_[platidx].unlocked = true;
    }
}

void Gui::trophyUnlockErr() {
}

void Gui::updateMemory(uint32_t addr, uint8_t type, const void *data) {
    for (auto &p: searchResults_) {
        if (p.addr == addr) {
            char value[64];
            cmd_->formatTypeData(value, type, data);
            p.type = type;
            p.value = value;
            break;
        }
    }
    for (auto &p: memTable_) {
        if (p.addr == addr) {
            char value[64];
            cmd_->formatTypeData(value, type, data);
            p.type = type;
            p.value = value;
            break;
        }
    }
    uint32_t e = memAddr_ + (uint32_t)memViewData_.size();
    if (addr >= memAddr_ && addr < e) {
        int sz = cmd_->getTypeSize(type, data);
        if (sz >= 0) {
            if (addr + sz > e) sz = e - addr;
            memcpy(&memViewData_[addr - memAddr_], data, sz);
        }
    }
}

void Gui::setMemViewData(uint32_t addr, const void *data, int len) {
    memAddr_ = addr;
    snprintf(memoryAddr_, 9, "%08X", memAddr_);
    memViewData_.assign((const uint8_t*)data, (const uint8_t*)data + len);
}

inline void Gui::connectPanel() {
    if (client_->isConnected()) {
        ImGui::Text("%s - %s", client_->titleId().c_str(), client_->title().c_str());
        langPanel();
        if (ImGui::Button(LS(DISCONNECT), ImVec2(100.f, 0.f))) {
            searchResults_.clear();
            searchStatus_ = 0;
            trophies_.clear();
            trophyStatus_ = 0;
            client_->disconnect();
        }
    } else {
        if (client_->isConnecting()) {
            ImGui::Text(LS(CONNECTING));
            langPanel();
            if (ImGui::Button(LS(DISCONNECT), ImVec2(100.f, 0.f))) {
                client_->disconnect();
            }
            ImGui::SameLine();
            ImGui::InvisibleButton(LS(AUTOCONNECT), ImVec2(100.f, 0.f));
        } else {
            ImGui::Text(LS(NOT_CONNECTED));
            langPanel();
            if (ImGui::Button(LS(CONNECT), ImVec2(100.f, 0.f))) {
                client_->connect(ip_, 9527);
            }
            ImGui::SameLine();
            if (ImGui::Button(LS(AUTOCONNECT), ImVec2(100.f, 0.f))) {
                client_->autoconnect(9527);
            }
        }
    }
    ImGui::SameLine();
    ImGui::Text(LS(IP_ADDR)); ImGui::SameLine();
    ImGui::PushItemWidth(200.f);
    ImGui::InputText("##IP", ip_, 256, client_->isConnected() ? ImGuiInputTextFlags_ReadOnly : 0);
    ImGui::PopItemWidth();
}

void Gui::langPanel() {
    ImGui::SameLine(400.f);
    auto *currlang = &g_lang.currLang();
    ImGui::PushItemWidth(200.f);
    if (ImGui::BeginCombo("##Lang", currlang->name().c_str())) {
        const auto &lmap = g_lang.langs();
        for (auto &p: lmap) {
            bool selected = &p.second == currlang;
            if (ImGui::Selectable(p.second.name().c_str(), selected)) {
                g_lang.setLanguage(p.first);
                reloadLang_ = true;
            }
            if (selected) ImGui::SetItemDefaultFocus();
        }
        ImGui::EndCombo();
    }
    ImGui::PopItemWidth();
}

inline void Gui::tabPanel() {
    ImGui::RadioButton(LS(MEM_SEARCHER), &tabIndex_, 0);
    ImGui::SameLine();
    ImGui::RadioButton(LS(MEM_VIEWER), &tabIndex_, 1);
    ImGui::SameLine();
    ImGui::RadioButton(LS(MEM_TABLE), &tabIndex_, 2);
    ImGui::SameLine();
    ImGui::RadioButton(LS(TROPHY), &tabIndex_, 3);
}

inline void formatData(uint8_t type, const char *src, bool isHex, void *dst) {
    switch (type) {
        case Command::st_autouint: case Command::st_u64:
        {
            uint64_t val = strtoull(src, NULL, isHex ? 16 : 10);
            memcpy(dst, &val, 8);
            break;
        }
        case Command::st_autoint: case Command::st_i64:
        {
            int64_t val = strtoll(src, NULL, isHex ? 16 : 10);
            memcpy(dst, &val, 8);
            break;
        }
        case Command::st_u32: case Command::st_u16: case Command::st_u8:
        {
            uint32_t val = strtoul(src, NULL, isHex ? 16 : 10);
            memcpy(dst, &val, 4);
            break;
        }
        case Command::st_i32: case Command::st_i16: case Command::st_i8:
        {
            int32_t val = strtol(src, NULL, isHex ? 16 : 10);
            memcpy(dst, &val, 4);
            break;
        }
        case Command::st_float:
        {
            float val = strtof(src, NULL);
            memcpy(dst, &val, 4);
            break;
        }
        case Command::st_double:
        {
            double val = strtod(src, NULL);
            memcpy(dst, &val, 8);
            break;
        }
    }
}

const uint8_t comboItemType[] = {
    Command::st_autoint, Command::st_autouint, Command::st_i32, Command::st_u32,
    Command::st_i16, Command::st_u16, Command::st_i8, Command::st_u8,
    Command::st_i64, Command::st_u64, Command::st_float, Command::st_double,
};

inline void Gui::searchPanel() {
    if (searchStatus_ == 1) {
        ImGui::Button(LS(SEARCHING), ImVec2(100.f, 0.f));
    } else {
        if (ImGui::Button(LS(NEW_SEARCH), ImVec2(100.f, 0.f)) && typeComboIndex_ >= 0) {
            char output[8];
            formatData(comboItemType[typeComboIndex_], searchVal_, hexSearch_, output);
            cmd_->startSearch(comboItemType[typeComboIndex_], heapSearch_, output);
        }
        if ((searchStatus_ == 2 || searchStatus_ == 3) && (ImGui::SameLine(), ImGui::Button(LS(NEXT_SEARCH), ImVec2(70.f, 0.f)) && typeComboIndex_ >= 0)) {
            char output[8];
            formatData(searchResultType_, searchVal_, hexSearch_, output);
            cmd_->nextSearch(output);
        }
    }
    ImGui::SameLine(); ImGui::Text(LS(VALUE)); ImGui::SameLine();
    ImGui::PushItemWidth(120.f);
    ImGui::InputText("##Value", searchVal_, 31, hexSearch_ ? ImGuiInputTextFlags_CharsHexadecimal : ImGuiInputTextFlags_CharsDecimal);
    ImGui::PopItemWidth();
    ImGui::SameLine();
    ImGui::PushItemWidth(100.f);
    if (ImGui::BeginCombo("##Type", LS(DATATYPE_FIRST + typeComboIndex_), 0)) {
        for (int i = 0; i < 10; ++i) {
            bool selected = typeComboIndex_ == i;
            if (ImGui::Selectable(LS(DATATYPE_FIRST + i), selected)) {
                if (typeComboIndex_ != i) {
                    typeComboIndex_ = i;
                    if (searchResultType_ != comboItemType[i] || searchResults_.empty()) {
                        searchStatus_ = 0;
                    } else {
                        searchStatus_ = 2;
                    }
                }
            }
            if (selected) ImGui::SetItemDefaultFocus();
        }
        ImGui::EndCombo();
    }
    ImGui::PopItemWidth();
    ImGui::SameLine();
    if (ImGui::Checkbox(LS(HEX), &hexSearch_)) {
        searchVal_[0] = 0;
    }
    ImGui::SameLine();
    ImGui::Checkbox(LS(HEAP), &heapSearch_);

    switch (searchStatus_) {
        case 2:
        {
            ImGui::Text(LS(SEARCH_RESULT));
            if (ImGui::ListBoxHeader("##Result")) {
                ImGui::Columns(2, NULL, true);
                int sz = searchResults_.size();
                for (int i = 0; i < sz; ++i) {
                    bool selected = searchResultIdx_ == i;
                    if (ImGui::Selectable(searchResults_[i].hexaddr.c_str(), selected, ImGuiSelectableFlags_SpanAllColumns | ImGuiSelectableFlags_AllowDoubleClick)) {
                        searchResultIdx_ = i;
                        if (ImGui::IsMouseDoubleClicked(0)) {
                            strcpy(searchEditVal_, searchResults_[i].value.c_str());
                            searchEditing_ = true;
                            searchEditHex_ = hexSearch_;
                        }
                    }
                    if (selected) ImGui::SetItemDefaultFocus();
                    ImGui::NextColumn();
                    ImGui::Text(searchResults_[i].value.c_str());
                    ImGui::NextColumn();
                }
                ImGui::ListBoxFooter();
            }
            if (searchResultIdx_ >= 0) {
                if (ImGui::Button(LS(EDIT_MEM))) {
                    strcpy(searchEditVal_, searchResults_[searchResultIdx_].value.c_str());
                    searchEditing_ = true;
                }
                ImGui::SameLine();
                if (ImGui::Button(LS(VIEW_MEMORY))) {
                    tabIndex_ = 1;
                    memAddr_ = searchResults_[searchResultIdx_].addr & ~0xFF;
                    snprintf(memoryAddr_, 9, "%08X", memAddr_);
                    memViewData_.clear();
                    memViewIndex_ = -1;
                    cmd_->readMem(memAddr_);
                }
                ImGui::SameLine();
                if (ImGui::Button(LS(ADD_TO_TABLE))) {
                    tabIndex_ = 2;
                    memTable_.push_back(searchResults_[searchResultIdx_]);
                    memTableIdx_ = (int)memTable_.size() - 1;
                }
            }
            break;
        }
        case 3:
            ImGui::Text(LS(TOO_MANY_RESULTS));
            break;
        case 4:
            ImGui::Text(LS(SEARCH_IN_PROGRESS));
            break;
    }
}

inline void Gui::searchPopup() {
    if (!searchEditing_) return;
    ImGui::OpenPopup(LS(POPUP_EDIT));
    if (ImGui::BeginPopupModal(LS(POPUP_EDIT), NULL, ImGuiWindowFlags_AlwaysAutoResize)) {
        auto &mi = searchResults_[searchResultIdx_];
        ImGui::Text("%s: %s", LS(EDIT_ADDR), mi.hexaddr.c_str());
        ImGui::InputText("##EditValue", searchEditVal_, 31, hexSearch_ ? ImGuiInputTextFlags_CharsHexadecimal : ImGuiInputTextFlags_CharsDecimal);
        ImGui::SameLine();
        if (ImGui::Checkbox(LS(HEX), &searchEditHex_)) {
            searchEditVal_[0] = 0;
        }
        if (ImGui::Button(LS(OK))) {
            searchEditing_ = false;
            ImGui::CloseCurrentPopup();
            char output[8];
            formatData(mi.type, searchEditVal_, hexSearch_, output);
            cmd_->modifyMemory(mi.type, mi.addr, output);
            searchEditVal_[0] = 0;
        }
        ImGui::SameLine();
        if (ImGui::Button(LS(CANCEL))) {
            searchEditing_ = false;
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
}

inline void Gui::memoryPanel() {
    if (ImGui::Button("<##PageUp") && memAddr_ != 0) {
        memAddr_ -= 0x100;
        snprintf(memoryAddr_, 9, "%08X", memAddr_);
        memViewData_.clear();
        memViewIndex_ = -1;
        cmd_->readMem(memAddr_);
    }
    ImGui::SameLine();
    if (ImGui::Button(">##PageDown") && memAddr_ != 0) {
        memAddr_ += 0x100;
        snprintf(memoryAddr_, 9, "%08X", memAddr_);
        memViewData_.clear();
        memViewIndex_ = -1;
        cmd_->readMem(memAddr_);
    }
    ImGui::SameLine(100.f);
    ImGui::Text(LS(EDIT_ADDR));
    ImGui::SameLine();
    ImGui::PushItemWidth(80.f);
    ImGui::InputText("##MemViewAddr", memoryAddr_, 9, ImGuiInputTextFlags_CharsHexadecimal);
    ImGui::PopItemWidth();
    ImGui::SameLine();
    if (ImGui::Button("->##GotoMemAddr")) {
        memAddr_ = (uint32_t)strtoul(memoryAddr_, NULL, 16) & ~0xFF;
        snprintf(memoryAddr_, 9, "%08X", memAddr_);
        memViewData_.clear();
        memViewIndex_ = -1;
        cmd_->readMem(memAddr_);
    }
    size_t sz = memViewData_.size();
    size_t lines = sz / 0x10;
    for (size_t i = 0; i < lines; ++i) {
        size_t curr = i * 0x10;
        size_t end = curr + 0x10;
        size_t count;
        if (end > sz) {
            end = sz; count = end - curr;
        } else count = 0x10;
        ImGui::Text("%08X", memAddr_ + curr);
        float pos = 65.f;
        for (size_t j = 0; j < count; ++j, ++curr) {
            pos += j == 8 ? 35.f : 25.f;
            ImGui::SameLine(pos);
            char n[3];
            snprintf(n, 3, "%02X##%d", memViewData_[curr], curr);
            if (ImGui::Selectable(n, memViewIndex_ == curr, ImGuiSelectableFlags_AllowDoubleClick, ImVec2(20.f, 20.f))) {
                memoryEditingAddr_ = memAddr_ + curr;
                if (ImGui::IsMouseDoubleClicked(0)) {
                    memoryEditing_ = true;
                    snprintf(memoryEditVal_, 3, "%02X", memViewData_[curr]);
                }
            }
        }
    }
}

inline void Gui::memoryPopup() {
    if (!memoryEditing_) return;
    ImGui::OpenPopup(LS(POPUP_EDIT));
    if (ImGui::BeginPopupModal(LS(POPUP_EDIT), NULL, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::Text("%s: %08X", LS(EDIT_ADDR), memoryEditingAddr_);
        ImGui::InputText("##MemEditValue", memoryEditVal_, 3, ImGuiInputTextFlags_CharsHexadecimal);
        if (ImGui::Button(LS(OK))) {
            memoryEditing_ = false;
            ImGui::CloseCurrentPopup();
            char output[8];
            formatData(Command::st_u8, memoryEditVal_, true, output);
            cmd_->modifyMemory(Command::st_u8, memoryEditingAddr_, output);
            memoryEditVal_[0] = 0;
        }
        ImGui::SameLine();
        if (ImGui::Button(LS(CANCEL))) {
            memoryEditing_ = false;
            ImGui::CloseCurrentPopup();
            memoryEditVal_[0] = 0;
        }
        ImGui::EndPopup();
    }
}

inline void Gui::tablePanel() {
    if (ImGui::ListBoxHeader("##MemTable", ImVec2(dispWidth_ - 30.f, 420.f))) {
        ImGui::Columns(5, NULL, true);
        ImGui::SetColumnWidth(0, 90.f);
        ImGui::SetColumnWidth(1, 70.f);
        ImGui::SetColumnWidth(2, 70.f);
        ImGui::SetColumnWidth(3, 20.f);
        ImGui::SetColumnWidth(4, dispWidth_ - 30.f - 270.f);
        int sz = memTable_.size();
        for (int i = 0; i < sz; ++i) {
            bool selected = memTableIdx_ == i;
            if (ImGui::Selectable(memTable_[i].hexaddr.c_str(), selected, ImGuiSelectableFlags_SpanAllColumns | ImGuiSelectableFlags_AllowDoubleClick)) {
                memTableIdx_ = i;
                if (ImGui::IsMouseDoubleClicked(0)) {
                    strcpy(tableEditVal_, memTable_[i].value.c_str());
                    tableEditing_ = true;
                }
            }
            if (selected) ImGui::SetItemDefaultFocus();
            ImGui::NextColumn();
            ImGui::Text(getTypeName(memTable_[i].type));
            ImGui::NextColumn();
            ImGui::Text(memTable_[i].value.c_str());
            ImGui::NextColumn();
            char llable[16];
            snprintf(llable, 16, "##lk%d", i);
            ImGui::Checkbox(llable, &memTable_[i].locked);
            ImGui::NextColumn();
            ImGui::Text(memTable_[i].comment.c_str());
            ImGui::NextColumn();
        }
        ImGui::ListBoxFooter();
    }
    if (memTableIdx_ >= 0) {
        if (ImGui::Button(LS(EDIT_MEM))) {
            strcpy(tableEditVal_, memTable_[memTableIdx_].value.c_str());
            tableEditing_ = true;
            tableTypeComboIdx_ = 0;
            for (int i = 0; i < sizeof(comboItemType) / sizeof(int); ++i) {
                if (comboItemType[i] == memTable_[memTableIdx_].type) {
                    tableTypeComboIdx_ = i;
                    break;
                }
            }
        }
        ImGui::SameLine();
        if (ImGui::Button(LS(VIEW_MEMORY))) {
            tabIndex_ = 1;
            memAddr_ = memTable_[memTableIdx_].addr & ~0xFF;
            snprintf(memoryAddr_, 9, "%08X", memAddr_);
            memViewData_.clear();
            memViewIndex_ = -1;
            cmd_->readMem(memAddr_);
        }
        ImGui::SameLine();
        if (ImGui::Button(LS(TABLE_DELETE))) {
            memTable_.erase(memTable_.begin() + memTableIdx_);
        }
        ImGui::SameLine();
        if (ImGui::Button(LS(TABLE_MODIFY))) {
            snprintf(tableModAddr_, 9, "%08X", memTable_[memTableIdx_].addr);
            strncpy(tableModComment_, memTable_[memTableIdx_].comment.c_str(), 64);
            tableModding_ = true;
            tableModAdding_ = false;
        }
        ImGui::SameLine();
    }
    if(ImGui::Button(LS(TABLE_ADD))) {
        tableModAddr_[0] = 0;
        tableModComment_[0] = 0;
        tableModding_ = true;
        tableModAdding_ = true;
    }
    if (ImGui::Button(LS(TABLE_SAVE))) {
        saveTable(client_->titleId().c_str());
    }
    ImGui::SameLine();
    if (ImGui::Button(LS(TABLE_LOAD))) {
        loadTable(client_->titleId().c_str());
    }
}

inline void Gui::tablePopup() {
    if (tableModding_) {
        ImGui::OpenPopup(LS(TABLE_MODIFY));
        if (ImGui::BeginPopupModal(LS(TABLE_MODIFY), NULL, ImGuiWindowFlags_AlwaysAutoResize)) {
            ImGui::Text(LS(TABLE_ADDR)); ImGui::SameLine(60.f);
            ImGui::InputText("##TableAddr", tableModAddr_, 16, ImGuiInputTextFlags_CharsHexadecimal);
            ImGui::Text(LS(TABLE_COMMENT)); ImGui::SameLine(60.f);
            ImGui::InputText("##TableComment", tableModComment_, 64, 0);
            if (ImGui::Button(LS(OK))) {
                tableModding_ = false;
                ImGui::CloseCurrentPopup();
                uint32_t addr;
                addr = strtoul(tableModAddr_, NULL, 16);
                char hexaddr[9];
                snprintf(hexaddr, 9, "%08X", addr);
                if (tableModAdding_) {
                    MemoryItem mi;
                    mi.addr = addr;
                    mi.hexaddr = hexaddr;
                    mi.comment = tableModComment_;
                    mi.type = Command::st_autoint;
                    memTable_.push_back(mi);
                    if (memTableIdx_ < 0) memTableIdx_ = (int)memTable_.size() - 1;
                } else if (memTableIdx_ >= 0 && memTableIdx_ < (int)memTable_.size()) {
                    auto &mi = memTable_[memTableIdx_];
                    mi.addr = addr;
                    mi.hexaddr = hexaddr;
                    mi.comment = tableModComment_;
                }
                tableModAdding_ = false;
                tableModAddr_[0] = 0;
                tableModComment_[0] = 0;
            }
            ImGui::SameLine();
            if (ImGui::Button(LS(CANCEL))) {
                tableModding_ = false;
                tableModAdding_ = false;
                tableModAddr_[0] = 0;
                tableModComment_[0] = 0;
                ImGui::CloseCurrentPopup();
            }
            ImGui::EndPopup();
        }
    }
    if (tableEditing_) {
        ImGui::OpenPopup(LS(EDIT_MEM));
        if (ImGui::BeginPopupModal(LS(EDIT_MEM), NULL, ImGuiWindowFlags_AlwaysAutoResize)) {
            ImGui::Text("%s: %08X", LS(EDIT_ADDR), memTable_[memTableIdx_].addr);
            ImGui::PushItemWidth(120.f);
            ImGui::InputText("##TableMemEdit", tableEditVal_, 31, ImGuiInputTextFlags_CharsHexadecimal);
            ImGui::PopItemWidth();
            ImGui::SameLine();
            ImGui::PushItemWidth(100.f);
            if (ImGui::BeginCombo("##Type", LS(DATATYPE_FIRST + tableTypeComboIdx_), 0)) {
                for (int i = 0; i < 10; ++i) {
                    bool selected = tableTypeComboIdx_ == i;
                    if (ImGui::Selectable(LS(DATATYPE_FIRST + i), selected)) {
                        if (tableTypeComboIdx_ != i)
                            tableTypeComboIdx_ = i;
                    }
                    if (selected) ImGui::SetItemDefaultFocus();
                }
                ImGui::EndCombo();
            }
            ImGui::PopItemWidth();
            ImGui::SameLine();
            if (ImGui::Checkbox(LS(HEX), &tableHex_)) {
                tableEditVal_[0] = 0;
            }
            if (ImGui::Button(LS(OK))) {
                tableEditing_ = false;
                ImGui::CloseCurrentPopup();
                char output[8];
                int type = comboItemType[tableTypeComboIdx_];
                if (type != memTable_[memTableIdx_].type) memTable_[memTableIdx_].type = type;
                formatData(type, tableEditVal_, true, output);
                cmd_->modifyMemory(type, memTable_[memTableIdx_].addr, output);
                tableEditVal_[0] = 0;
            }
            ImGui::SameLine();
            if (ImGui::Button(LS(CANCEL))) {
                tableEditing_ = false;
                ImGui::CloseCurrentPopup();
                tableEditVal_[0] = 0;
            }
            ImGui::EndPopup();
        }
    }
}

inline void Gui::trophyPanel() {
    switch (trophyStatus_) {
        case 0:
        case 2:
            if (ImGui::Button(LS(REFRESH_TROPHY))) {
                trophyStatus_ = 1;
                trophyIdx_ = -1;
                trophyPlat_ = -1;
                trophies_.clear();
                cmd_->refreshTrophy();
            }
            break;
        case 1:
            ImGui::Text(LS(REFRESHING));
            break;
    }
    int sz = trophies_.size();
    if (sz == 0) return;
    if (ImGui::ListBoxHeader("##Trophies", ImVec2(dispWidth_ - 30.f, 420.f))) {
        ImGui::Columns(4, NULL, true);
        ImGui::SetColumnWidth(0, dispWidth_ - 30.f - 230.f);
        ImGui::SetColumnWidth(1, 70.f);
        ImGui::SetColumnWidth(2, 70.f);
        ImGui::SetColumnWidth(3, 70.f);
        ImGui::Text(LS(TROPHY_NAME));
        ImGui::NextColumn();
        ImGui::Text(LS(TROPHY_GRADE));
        ImGui::NextColumn();
        ImGui::Text(LS(TROPHY_HIDDEN));
        ImGui::NextColumn();
        ImGui::Text(LS(TROPHY_UNLOCKED));
        ImGui::NextColumn();
        for (int i = 0; i < sz; ++i) {
            auto &t = trophies_[i];
            char hiddenname[32];
            if (ImGui::Selectable(t.name.empty() ? (snprintf(hiddenname, 32, LS(TROPHY_TITLE_HIDDEN), i), hiddenname) : t.name.c_str(), trophyIdx_ == i, ImGuiSelectableFlags_SpanAllColumns))
                trophyIdx_ = i;
            if (ImGui::IsItemHovered() && !t.desc.empty())
                ImGui::SetTooltip("%s", t.desc.c_str());
            ImGui::NextColumn();
            ImGui::Text(getGradeName(t.grade));
            ImGui::NextColumn();
            if (t.hidden)
                ImGui::Text(LS(YES));
            ImGui::NextColumn();
            if (t.unlocked)
                ImGui::Text(LS(YES));
            ImGui::NextColumn();
        }
        ImGui::ListBoxFooter();
    }
    if (trophyIdx_ >= 0 && trophyIdx_ < sz && trophies_[trophyIdx_].grade != 1 && !trophies_[trophyIdx_].unlocked) {
        if (ImGui::Button(LS(TROPHY_UNLOCK))) {
            cmd_->unlockTrophy(trophies_[trophyIdx_].id, trophies_[trophyIdx_].hidden);
        }
    }
    if (trophyPlat_ >= 0 && !trophies_[trophyPlat_].unlocked) {
        if (ImGui::Button(LS(TROPHY_UNLOCK_ALL))) {
            uint32_t data[4];
            for (auto &t: trophies_) {
                if (t.hidden)
                    data[t.id >> 5] |= 1U << (t.id & 0x1F);
            }
            cmd_->unlockAllTrophy(data);
        }
    }
}

inline void getConfigFilePath(char *path) {
#ifdef _WIN32
    GetModuleFileNameA(NULL, path, 256);
    PathRemoveFileSpecA(path);
    PathAppendA(path, "rcsvr.yml");
#else
    strcpy(path, "rcsvr.yml");
#endif
}

inline void Gui::saveData() {
    char path[256];
    getConfigFilePath(path);
    YAML::Emitter out;
    out << YAML::BeginMap;
    const auto &name = g_lang.currLang().id();
    if (!name.empty()) out << YAML::Key << "Lang" << YAML::Value << name;
    out << YAML::Key << "IPAddr" << YAML::Value << ip_;
    out << YAML::Key << "HEX" << YAML::Value << hexSearch_;
    out << YAML::Key << "HEAP" << YAML::Value << heapSearch_;
    out << YAML::EndMap;
    std::ofstream f(path);
    f << out.c_str();
    f.close();
}

inline void Gui::loadData() {
    char path[256];
    getConfigFilePath(path);
    YAML::Node node;
    try {
        node = YAML::LoadFile(path);
        if (node["Lang"].IsDefined()) g_lang.setLanguage(node["Lang"].as<std::string>());
        if (node["IPAddr"].IsDefined()) strncpy(ip_, node["IPAddr"].as<std::string>().c_str(), 256);
        if (node["HEX"].IsDefined()) hexSearch_ = node["HEX"].as<bool>();
        if (node["HEAP"].IsDefined()) heapSearch_ = node["HEAP"].as<bool>();
    } catch (...) { return; }
}

inline void getTableFilePath(char *path, const char *name) {
#ifdef _WIN32
    GetModuleFileNameA(NULL, path, 256);
    PathRemoveFileSpecA(path);
    PathAppendA(path, "tables");
    CreateDirectoryA(path, NULL);
    PathAppendA(path, name);
    lstrcatA(path, ".yml");
#else
    mkdir("tables", 0755);
    sprintf(path, "tables/%s.yml", name);
#endif
}

void Gui::saveTable(const char *name) {
    char path[256];
    getTableFilePath(path, name);
    YAML::Emitter out;
    out << YAML::BeginMap;
    out << YAML::Key << "List" << YAML::BeginSeq;
    for (auto &p: memTable_) {
        out << YAML::BeginMap;
        out << YAML::Key << "Type" << YAML::Value << YAML::Dec << p.type;
        out << YAML::Key << "Addr" << YAML::Value << YAML::Hex << p.addr;
        out << YAML::Key << "Comment" << YAML::Value << p.comment;
        out << YAML::EndMap;
    }
    out << YAML::EndSeq;
    out << YAML::EndMap;
    std::ofstream f(path);
    f << out.c_str();
    f.close();
}

bool Gui::loadTable(const char *name) {
    char path[256];
    getTableFilePath(path, name);
    YAML::Node node;
    try {
        node = YAML::LoadFile(path);
        for (auto &p: node["List"]) {
            MemoryItem mi;
            mi.type = p["Type"].as<int>();
            mi.addr = p["Addr"].as<uint32_t>();
            mi.comment = p["Comment"].as<std::string>();
            char hex[16];
            snprintf(hex, 16, "%08X", mi.addr);
            mi.hexaddr = hex;
            memTable_.emplace_back(mi);
        }
    } catch (...) { return false; }
    return true;
}

void Gui::reloadFonts() {
    ImGui_ImplGlfwGL3_InvalidateDeviceObjects();
    char title[256];
    snprintf(title, 256, "%s v" VERSION_STR, LS(WINDOW_TITLE));
    glfwSetWindowTitle(window_, title);
    ImFontAtlas *f = ImGui::GetIO().Fonts;
    static const ImWchar ranges[] =
    {
        0x0020, 0x00FF, // Basic Latin + Latin Supplement
        0x2000, 0x31FF, // Katakana Phonetic Extensions
        0x4E00, 0x9FAF, // CJK Ideograms
        0xFF00, 0xFFEF, // Half-width characters
        0,
    };
    f->ClearInputData();
    f->ClearTexData();
    f->ClearFonts();
    if (!f->AddFontFromFileTTF("font.ttc", 18.0f, NULL, ranges)) {
        bool found = false;
        for (auto &p: g_lang.currLang().fonts()) {
            if (f->AddFontFromFileTTF(p.c_str(), 18.0f, NULL, ranges)) {
                found = true;  break;
            }
        }
        if (!found) f->AddFontDefault();
    }
}
