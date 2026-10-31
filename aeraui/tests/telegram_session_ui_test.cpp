// SPDX-License-Identifier: Apache-2.0
#include "scene.hpp"
#include "ui_components.hpp"
#include "telegram/protocol.hpp"
#include <cassert>
#include <cerrno>
#include <cstring>
#include <sys/socket.h>
#include <unistd.h>
#include <vector>
namespace aeraui {
void BuildTelegramSessionTestScene(lv_obj_t *, bool, bool, int);
}
namespace {
using namespace aeraui;
void Tick() { for (int i=0;i<8;++i) { lv_tick_inc(25); lv_timer_handler(); } }
lv_obj_t *Find(lv_obj_t *root, const char *text) {
  if (lv_obj_check_type(root,&lv_label_class) && !strcmp(lv_label_get_text(root),text))
    return lv_obj_get_parent(root);
  for (uint32_t i=0;i<lv_obj_get_child_count(root);++i)
    if (auto *p=Find(lv_obj_get_child(root,i),text)) return p;
  return nullptr;
}
void Click(lv_obj_t *root, const char *text) {
  auto *p=Find(root,text); assert(p); lv_obj_send_event(p,LV_EVENT_CLICKED,nullptr); Tick();
}
void Inputs(lv_obj_t *root,std::vector<lv_obj_t *> &out) {
  if (lv_obj_check_type(root,&lv_textarea_class)) out.push_back(root);
  for (uint32_t i=0;i<lv_obj_get_child_count(root);++i) Inputs(lv_obj_get_child(root,i),out);
}
void Type(lv_obj_t *root,const char *first,const char *second=nullptr) {
  std::vector<lv_obj_t *> fields; Inputs(root,fields);
  assert(fields.size()==(second?2u:1u));
  lv_textarea_set_text(fields[0],first);
  if (second) lv_textarea_set_text(fields[1],second);
}
telegram::Message Receive(int fd) {
  telegram::Message m;
  assert(recv(fd,&m,sizeof(m),MSG_DONTWAIT)==sizeof(m));return m;
}
void Reply(int fd,telegram::Kind kind,uint32_t value,const char *text="") {
  telegram::Message m;m.kind=kind;m.value=value;m.secondary=1;
  strcpy(m.text,text);assert(send(fd,&m,sizeof(m),0)==sizeof(m));Tick();
}
}
void RunTelegramSessionUiTests(lv_display_t *display,lv_obj_t *home) {
  using namespace aeraui;
  for (bool landscape : {false,true}) {
    lv_display_set_resolution(display,landscape?3168:1440,landscape?1440:3168);
    for (bool locked : {false,true}) {
      int fd[2]; assert(socketpair(AF_UNIX,SOCK_SEQPACKET|SOCK_CLOEXEC,0,fd)==0);
      auto *page=lv_obj_create(nullptr);
      BuildTelegramSessionTestScene(page,true,locked,fd[0]);lv_screen_load(page);Tick();
      if (!locked) {
        assert(Receive(fd[1]).kind==telegram::Kind::kLoadChats);
        Click(page,LV_SYMBOL_SETTINGS);
      }
      Click(page,"Change vault password");
      assert(Find(page,"Current vault password"));
      Type(page,"incorrect-password");Click(page,"Continue");
      if (locked) {
        auto m=Receive(fd[1]);assert(m.kind==telegram::Kind::kConfigure);
        Reply(fd[1],telegram::Kind::kError,0,"Could not unlock the Telegram vault.");
      }
      assert(Find(page,"Incorrect vault password."));
      Type(page,"old-password");Click(page,"Continue");
      if (locked) {
        auto m=Receive(fd[1]);assert(m.kind==telegram::Kind::kConfigure);
        assert(!strcmp(m.text,"old-password"));
        Reply(fd[1],telegram::Kind::kState,static_cast<uint32_t>(telegram::AuthState::kReady));
      }
      assert(Find(page,"New password"));
      Type(page,"short","short");Click(page,"Change password");
      assert(Find(page,"Use at least 8 characters for the vault password."));
      Type(page,"new-password","different-password");Click(page,"Change password");
      assert(Find(page,"The two passwords do not match. Please try again."));
      Type(page,"new-password","new-password");Click(page,"Change password");
      auto m=Receive(fd[1]);assert(m.kind==telegram::Kind::kChangeVaultPassword);
      assert(!strcmp(m.text,"new-password"));
      Reply(fd[1],telegram::Kind::kVaultPasswordResult,0);
      assert(Find(page,"Could not change the vault password."));
      Type(page,"new-password","new-password");Click(page,"Change password");
      assert(Receive(fd[1]).kind==telegram::Kind::kChangeVaultPassword);
      Reply(fd[1],telegram::Kind::kVaultPasswordResult,1);
      assert(Find(page,"Vault password changed."));
      // Cached key must change only on the successful reply.
      Click(page,"Change vault password");Type(page,"old-password");Click(page,"Continue");
      assert(Find(page,"Incorrect vault password."));
      Type(page,"new-password");Click(page,"Continue");assert(Find(page,"New password"));
      lv_screen_load(home);lv_obj_delete(page);close(fd[1]);Tick();
    }
    // Older runtime: update notice, no extended command, reset still accessible.
    int fd[2];assert(socketpair(AF_UNIX,SOCK_SEQPACKET|SOCK_CLOEXEC,0,fd)==0);
    auto *page=lv_obj_create(nullptr);
    BuildTelegramSessionTestScene(page,false,true,fd[0]);lv_screen_load(page);Tick();
    Click(page,"Change vault password");assert(Find(page,"Update Telegram plugin"));
    assert(widgets::DismissModal(page));Tick();
    Click(page,"Forgot password?");assert(Find(page,"Reset local Telegram session?"));
    assert(Find(page,"Swipe to reset"));
    assert(widgets::DismissModal(page));Tick();
    telegram::Message m; assert(recv(fd[1],&m,sizeof(m),MSG_DONTWAIT)<0 && errno==EAGAIN);
    lv_screen_load(home);lv_obj_delete(page);close(fd[1]);Tick();
  }
  puts("Telegram session: old-key validation, change success/failure, lock-screen flow and legacy runtime passed in both orientations.");
}
