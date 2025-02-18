// Copyright 2010 the V8 project authors. All rights reserved.
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions are
// met:
//
//     * Redistributions of source code must retain the above copyright
//       notice, this list of conditions and the following disclaimer.
//     * Redistributions in binary form must reproduce the above
//       copyright notice, this list of conditions and the following
//       disclaimer in the documentation and/or other materials provided
//       with the distribution.
//     * Neither the name of Google Inc. nor the names of its
//       contributors may be used to endorse or promote products derived
//       from this software without specific prior written permission.
//
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
// "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
// LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
// A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
// OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
// SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
// LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
// DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
// THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
// (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
// OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
//
// Tests of profiles generator and utilities.

#include "v8.h"
#include "profile-generator-inl.h"
#include "cctest.h"
#include "../include/v8-profiler.h"

using i::CodeEntry;
using i::CodeMap;
using i::CpuProfile;
using i::CpuProfiler;
using i::CpuProfilesCollection;
using i::ProfileNode;
using i::ProfileTree;
using i::ProfileGenerator;
using i::SampleRateCalculator;
using i::TickSample;
using i::TokenEnumerator;
using i::Vector;


namespace v8 {
namespace internal {

class TokenEnumeratorTester {
 public:
  static i::List<bool>* token_removed(TokenEnumerator* te) {
    return &te->token_removed_;
  }
};

} }  // namespace v8::internal

TEST(TokenEnumerator) {
  TokenEnumerator te;
  CHECK_EQ(TokenEnumerator::kNoSecurityToken, te.GetTokenId(NULL));
  v8::HandleScope hs(v8::Isolate::GetCurrent());
  v8::Local<v8::String> token1(v8::String::New("1x"));
  CHECK_EQ(0, te.GetTokenId(*v8::Utils::OpenHandle(*token1)));
  CHECK_EQ(0, te.GetTokenId(*v8::Utils::OpenHandle(*token1)));
  v8::Local<v8::String> token2(v8::String::New("2x"));
  CHECK_EQ(1, te.GetTokenId(*v8::Utils::OpenHandle(*token2)));
  CHECK_EQ(1, te.GetTokenId(*v8::Utils::OpenHandle(*token2)));
  CHECK_EQ(0, te.GetTokenId(*v8::Utils::OpenHandle(*token1)));
  {
    v8::HandleScope hs(v8::Isolate::GetCurrent());
    v8::Local<v8::String> token3(v8::String::New("3x"));
    CHECK_EQ(2, te.GetTokenId(*v8::Utils::OpenHandle(*token3)));
    CHECK_EQ(1, te.GetTokenId(*v8::Utils::OpenHandle(*token2)));
    CHECK_EQ(0, te.GetTokenId(*v8::Utils::OpenHandle(*token1)));
  }
  CHECK(!i::TokenEnumeratorTester::token_removed(&te)->at(2));
  HEAP->CollectAllGarbage(i::Heap::kNoGCFlags);
  CHECK(i::TokenEnumeratorTester::token_removed(&te)->at(2));
  CHECK_EQ(1, te.GetTokenId(*v8::Utils::OpenHandle(*token2)));
  CHECK_EQ(0, te.GetTokenId(*v8::Utils::OpenHandle(*token1)));
}


TEST(ProfileNodeFindOrAddChild) {
  ProfileTree tree;
  ProfileNode node(&tree, NULL);
  CodeEntry entry1(i::Logger::FUNCTION_TAG, "aaa");
  ProfileNode* childNode1 = node.FindOrAddChild(&entry1);
  CHECK_NE(NULL, childNode1);
  CHECK_EQ(childNode1, node.FindOrAddChild(&entry1));
  CodeEntry entry2(i::Logger::FUNCTION_TAG, "bbb");
  ProfileNode* childNode2 = node.FindOrAddChild(&entry2);
  CHECK_NE(NULL, childNode2);
  CHECK_NE(childNode1, childNode2);
  CHECK_EQ(childNode1, node.FindOrAddChild(&entry1));
  CHECK_EQ(childNode2, node.FindOrAddChild(&entry2));
  CodeEntry entry3(i::Logger::FUNCTION_TAG, "ccc");
  ProfileNode* childNode3 = node.FindOrAddChild(&entry3);
  CHECK_NE(NULL, childNode3);
  CHECK_NE(childNode1, childNode3);
  CHECK_NE(childNode2, childNode3);
  CHECK_EQ(childNode1, node.FindOrAddChild(&entry1));
  CHECK_EQ(childNode2, node.FindOrAddChild(&entry2));
  CHECK_EQ(childNode3, node.FindOrAddChild(&entry3));
}


TEST(ProfileNodeFindOrAddChildForSameFunction) {
  const char* aaa = "aaa";
  ProfileTree tree;
  ProfileNode node(&tree, NULL);
  CodeEntry entry1(i::Logger::FUNCTION_TAG, aaa);
  ProfileNode* childNode1 = node.FindOrAddChild(&entry1);
  CHECK_NE(NULL, childNode1);
  CHECK_EQ(childNode1, node.FindOrAddChild(&entry1));
  // The same function again.
  CodeEntry entry2(i::Logger::FUNCTION_TAG, aaa);
  CHECK_EQ(childNode1, node.FindOrAddChild(&entry2));
  // Now with a different security token.
  CodeEntry entry3(i::Logger::FUNCTION_TAG, aaa,
                   TokenEnumerator::kNoSecurityToken + 1);
  CHECK_EQ(childNode1, node.FindOrAddChild(&entry3));
}


namespace {

class ProfileTreeTestHelper {
 public:
  explicit ProfileTreeTestHelper(const ProfileTree* tree)
      : tree_(tree) { }

  ProfileNode* Walk(CodeEntry* entry1,
                    CodeEntry* entry2 = NULL,
                    CodeEntry* entry3 = NULL) {
    ProfileNode* node = tree_->root();
    node = node->FindChild(entry1);
    if (node == NULL) return NULL;
    if (entry2 != NULL) {
      node = node->FindChild(entry2);
      if (node == NULL) return NULL;
    }
    if (entry3 != NULL) {
      node = node->FindChild(entry3);
    }
    return node;
  }

 private:
  const ProfileTree* tree_;
};

}  // namespace

TEST(ProfileTreeAddPathFromStart) {
  CodeEntry entry1(i::Logger::FUNCTION_TAG, "aaa");
  CodeEntry entry2(i::Logger::FUNCTION_TAG, "bbb");
  CodeEntry entry3(i::Logger::FUNCTION_TAG, "ccc");
  ProfileTree tree;
  ProfileTreeTestHelper helper(&tree);
  CHECK_EQ(NULL, helper.Walk(&entry1));
  CHECK_EQ(NULL, helper.Walk(&entry2));
  CHECK_EQ(NULL, helper.Walk(&entry3));

  CodeEntry* path[] = {NULL, &entry1, NULL, &entry2, NULL, NULL, &entry3, NULL};
  Vector<CodeEntry*> path_vec(path, sizeof(path) / sizeof(path[0]));
  tree.AddPathFromStart(path_vec);
  CHECK_EQ(NULL, helper.Walk(&entry2));
  CHECK_EQ(NULL, helper.Walk(&entry3));
  ProfileNode* node1 = helper.Walk(&entry1);
  CHECK_NE(NULL, node1);
  CHECK_EQ(0, node1->total_ticks());
  CHECK_EQ(0, node1->self_ticks());
  CHECK_EQ(NULL, helper.Walk(&entry1, &entry1));
  CHECK_EQ(NULL, helper.Walk(&entry1, &entry3));
  ProfileNode* node2 = helper.Walk(&entry1, &entry2);
  CHECK_NE(NULL, node2);
  CHECK_NE(node1, node2);
  CHECK_EQ(0, node2->total_ticks());
  CHECK_EQ(0, node2->self_ticks());
  CHECK_EQ(NULL, helper.Walk(&entry1, &entry2, &entry1));
  CHECK_EQ(NULL, helper.Walk(&entry1, &entry2, &entry2));
  ProfileNode* node3 = helper.Walk(&entry1, &entry2, &entry3);
  CHECK_NE(NULL, node3);
  CHECK_NE(node1, node3);
  CHECK_NE(node2, node3);
  CHECK_EQ(0, node3->total_ticks());
  CHECK_EQ(1, node3->self_ticks());

  tree.AddPathFromStart(path_vec);
  CHECK_EQ(node1, helper.Walk(&entry1));
  CHECK_EQ(node2, helper.Walk(&entry1, &entry2));
  CHECK_EQ(node3, helper.Walk(&entry1, &entry2, &entry3));
  CHECK_EQ(0, node1->total_ticks());
  CHECK_EQ(0, node1->self_ticks());
  CHECK_EQ(0, node2->total_ticks());
  CHECK_EQ(0, node2->self_ticks());
  CHECK_EQ(0, node3->total_ticks());
  CHECK_EQ(2, node3->self_ticks());

  CodeEntry* path2[] = {&entry1, &entry2, &entry2};
  Vector<CodeEntry*> path2_vec(path2, sizeof(path2) / sizeof(path2[0]));
  tree.AddPathFromStart(path2_vec);
  CHECK_EQ(NULL, helper.Walk(&entry2));
  CHECK_EQ(NULL, helper.Walk(&entry3));
  CHECK_EQ(node1, helper.Walk(&entry1));
  CHECK_EQ(NULL, helper.Walk(&entry1, &entry1));
  CHECK_EQ(NULL, helper.Walk(&entry1, &entry3));
  CHECK_EQ(node2, helper.Walk(&entry1, &entry2));
  CHECK_EQ(NULL, helper.Walk(&entry1, &entry2, &entry1));
  CHECK_EQ(node3, helper.Walk(&entry1, &entry2, &entry3));
  CHECK_EQ(0, node3->total_ticks());
  CHECK_EQ(2, node3->self_ticks());
  ProfileNode* node4 = helper.Walk(&entry1, &entry2, &entry2);
  CHECK_NE(NULL, node4);
  CHECK_NE(node3, node4);
  CHECK_EQ(0, node4->total_ticks());
  CHECK_EQ(1, node4->self_ticks());
}


TEST(ProfileTreeAddPathFromEnd) {
  CodeEntry entry1(i::Logger::FUNCTION_TAG, "aaa");
  CodeEntry entry2(i::Logger::FUNCTION_TAG, "bbb");
  CodeEntry entry3(i::Logger::FUNCTION_TAG, "ccc");
  ProfileTree tree;
  ProfileTreeTestHelper helper(&tree);
  CHECK_EQ(NULL, helper.Walk(&entry1));
  CHECK_EQ(NULL, helper.Walk(&entry2));
  CHECK_EQ(NULL, helper.Walk(&entry3));

  CodeEntry* path[] = {NULL, &entry3, NULL, &entry2, NULL, NULL, &entry1, NULL};
  Vector<CodeEntry*> path_vec(path, sizeof(path) / sizeof(path[0]));
  tree.AddPathFromEnd(path_vec);
  CHECK_EQ(NULL, helper.Walk(&entry2));
  CHECK_EQ(NULL, helper.Walk(&entry3));
  ProfileNode* node1 = helper.Walk(&entry1);
  CHECK_NE(NULL, node1);
  CHECK_EQ(0, node1->total_ticks());
  CHECK_EQ(0, node1->self_ticks());
  CHECK_EQ(NULL, helper.Walk(&entry1, &entry1));
  CHECK_EQ(NULL, helper.Walk(&entry1, &entry3));
  ProfileNode* node2 = helper.Walk(&entry1, &entry2);
  CHECK_NE(NULL, node2);
  CHECK_NE(node1, node2);
  CHECK_EQ(0, node2->total_ticks());
  CHECK_EQ(0, node2->self_ticks());
  CHECK_EQ(NULL, helper.Walk(&entry1, &entry2, &entry1));
  CHECK_EQ(NULL, helper.Walk(&entry1, &entry2, &entry2));
  ProfileNode* node3 = helper.Walk(&entry1, &entry2, &entry3);
  CHECK_NE(NULL, node3);
  CHECK_NE(node1, node3);
  CHECK_NE(node2, node3);
  CHECK_EQ(0, node3->total_ticks());
  CHECK_EQ(1, node3->self_ticks());

  tree.AddPathFromEnd(path_vec);
  CHECK_EQ(node1, helper.Walk(&entry1));
  CHECK_EQ(node2, helper.Walk(&entry1, &entry2));
  CHECK_EQ(node3, helper.Walk(&entry1, &entry2, &entry3));
  CHECK_EQ(0, node1->total_ticks());
  CHECK_EQ(0, node1->self_ticks());
  CHECK_EQ(0, node2->total_ticks());
  CHECK_EQ(0, node2->self_ticks());
  CHECK_EQ(0, node3->total_ticks());
  CHECK_EQ(2, node3->self_ticks());

  CodeEntry* path2[] = {&entry2, &entry2, &entry1};
  Vector<CodeEntry*> path2_vec(path2, sizeof(path2) / sizeof(path2[0]));
  tree.AddPathFromEnd(path2_vec);
  CHECK_EQ(NULL, helper.Walk(&entry2));
  CHECK_EQ(NULL, helper.Walk(&entry3));
  CHECK_EQ(node1, helper.Walk(&entry1));
  CHECK_EQ(NULL, helper.Walk(&entry1, &entry1));
  CHECK_EQ(NULL, helper.Walk(&entry1, &entry3));
  CHECK_EQ(node2, helper.Walk(&entry1, &entry2));
  CHECK_EQ(NULL, helper.Walk(&entry1, &entry2, &entry1));
  CHECK_EQ(node3, helper.Walk(&entry1, &entry2, &entry3));
  CHECK_EQ(0, node3->total_ticks());
  CHECK_EQ(2, node3->self_ticks());
  ProfileNode* node4 = helper.Walk(&entry1, &entry2, &entry2);
  CHECK_NE(NULL, node4);
  CHECK_NE(node3, node4);
  CHECK_EQ(0, node4->total_ticks());
  CHECK_EQ(1, node4->self_ticks());
}


TEST(ProfileTreeCalculateTotalTicks) {
  ProfileTree empty_tree;
  CHECK_EQ(0, empty_tree.root()->total_ticks());
  CHECK_EQ(0, empty_tree.root()->self_ticks());
  empty_tree.CalculateTotalTicks();
  CHECK_EQ(0, empty_tree.root()->total_ticks());
  CHECK_EQ(0, empty_tree.root()->self_ticks());
  empty_tree.root()->IncrementSelfTicks();
  CHECK_EQ(0, empty_tree.root()->total_ticks());
  CHECK_EQ(1, empty_tree.root()->self_ticks());
  empty_tree.CalculateTotalTicks();
  CHECK_EQ(1, empty_tree.root()->total_ticks());
  CHECK_EQ(1, empty_tree.root()->self_ticks());

  CodeEntry entry1(i::Logger::FUNCTION_TAG, "aaa");
  CodeEntry* e1_path[] = {&entry1};
  Vector<CodeEntry*> e1_path_vec(
      e1_path, sizeof(e1_path) / sizeof(e1_path[0]));

  ProfileTree single_child_tree;
  single_child_tree.AddPathFromStart(e1_path_vec);
  single_child_tree.root()->IncrementSelfTicks();
  CHECK_EQ(0, single_child_tree.root()->total_ticks());
  CHECK_EQ(1, single_child_tree.root()->self_ticks());
  ProfileTreeTestHelper single_child_helper(&single_child_tree);
  ProfileNode* node1 = single_child_helper.Walk(&entry1);
  CHECK_NE(NULL, node1);
  CHECK_EQ(0, node1->total_ticks());
  CHECK_EQ(1, node1->self_ticks());
  single_child_tree.CalculateTotalTicks();
  CHECK_EQ(2, single_child_tree.root()->total_ticks());
  CHECK_EQ(1, single_child_tree.root()->self_ticks());
  CHECK_EQ(1, node1->total_ticks());
  CHECK_EQ(1, node1->self_ticks());

  CodeEntry entry2(i::Logger::FUNCTION_TAG, "bbb");
  CodeEntry* e1_e2_path[] = {&entry1, &entry2};
  Vector<CodeEntry*> e1_e2_path_vec(
      e1_e2_path, sizeof(e1_e2_path) / sizeof(e1_e2_path[0]));

  ProfileTree flat_tree;
  ProfileTreeTestHelper flat_helper(&flat_tree);
  flat_tree.AddPathFromStart(e1_path_vec);
  flat_tree.AddPathFromStart(e1_path_vec);
  flat_tree.AddPathFromStart(e1_e2_path_vec);
  flat_tree.AddPathFromStart(e1_e2_path_vec);
  flat_tree.AddPathFromStart(e1_e2_path_vec);
  // Results in {root,0,0} -> {entry1,0,2} -> {entry2,0,3}
  CHECK_EQ(0, flat_tree.root()->total_ticks());
  CHECK_EQ(0, flat_tree.root()->self_ticks());
  node1 = flat_helper.Walk(&entry1);
  CHECK_NE(NULL, node1);
  CHECK_EQ(0, node1->total_ticks());
  CHECK_EQ(2, node1->self_ticks());
  ProfileNode* node2 = flat_helper.Walk(&entry1, &entry2);
  CHECK_NE(NULL, node2);
  CHECK_EQ(0, node2->total_ticks());
  CHECK_EQ(3, node2->self_ticks());
  flat_tree.CalculateTotalTicks();
  // Must calculate {root,5,0} -> {entry1,5,2} -> {entry2,3,3}
  CHECK_EQ(5, flat_tree.root()->total_ticks());
  CHECK_EQ(0, flat_tree.root()->self_ticks());
  CHECK_EQ(5, node1->total_ticks());
  CHECK_EQ(2, node1->self_ticks());
  CHECK_EQ(3, node2->total_ticks());
  CHECK_EQ(3, node2->self_ticks());

  CodeEntry* e2_path[] = {&entry2};
  Vector<CodeEntry*> e2_path_vec(
      e2_path, sizeof(e2_path) / sizeof(e2_path[0]));
  CodeEntry entry3(i::Logger::FUNCTION_TAG, "ccc");
  CodeEntry* e3_path[] = {&entry3};
  Vector<CodeEntry*> e3_path_vec(
      e3_path, sizeof(e3_path) / sizeof(e3_path[0]));

  ProfileTree wide_tree;
  ProfileTreeTestHelper wide_helper(&wide_tree);
  wide_tree.AddPathFromStart(e1_path_vec);
  wide_tree.AddPathFromStart(e1_path_vec);
  wide_tree.AddPathFromStart(e1_e2_path_vec);
  wide_tree.AddPathFromStart(e2_path_vec);
  wide_tree.AddPathFromStart(e2_path_vec);
  wide_tree.AddPathFromStart(e2_path_vec);
  wide_tree.AddPathFromStart(e3_path_vec);
  wide_tree.AddPathFromStart(e3_path_vec);
  wide_tree.AddPathFromStart(e3_path_vec);
  wide_tree.AddPathFromStart(e3_path_vec);
  // Results in            -> {entry1,0,2} -> {entry2,0,1}
  //            {root,0,0} -> {entry2,0,3}
  //                       -> {entry3,0,4}
  CHECK_EQ(0, wide_tree.root()->total_ticks());
  CHECK_EQ(0, wide_tree.root()->self_ticks());
  node1 = wide_helper.Walk(&entry1);
  CHECK_NE(NULL, node1);
  CHECK_EQ(0, node1->total_ticks());
  CHECK_EQ(2, node1->self_ticks());
  ProfileNode* node1_2 = wide_helper.Walk(&entry1, &entry2);
  CHECK_NE(NULL, node1_2);
  CHECK_EQ(0, node1_2->total_ticks());
  CHECK_EQ(1, node1_2->self_ticks());
  node2 = wide_helper.Walk(&entry2);
  CHECK_NE(NULL, node2);
  CHECK_EQ(0, node2->total_ticks());
  CHECK_EQ(3, node2->self_ticks());
  ProfileNode* node3 = wide_helper.Walk(&entry3);
  CHECK_NE(NULL, node3);
  CHECK_EQ(0, node3->total_ticks());
  CHECK_EQ(4, node3->self_ticks());
  wide_tree.CalculateTotalTicks();
  // Calculates             -> {entry1,3,2} -> {entry2,1,1}
  //            {root,10,0} -> {entry2,3,3}
  //                        -> {entry3,4,4}
  CHECK_EQ(10, wide_tree.root()->total_ticks());
  CHECK_EQ(0, wide_tree.root()->self_ticks());
  CHECK_EQ(3, node1->total_ticks());
  CHECK_EQ(2, node1->self_ticks());
  CHECK_EQ(1, node1_2->total_ticks());
  CHECK_EQ(1, node1_2->self_ticks());
  CHECK_EQ(3, node2->total_ticks());
  CHECK_EQ(3, node2->self_ticks());
  CHECK_EQ(4, node3->total_ticks());
  CHECK_EQ(4, node3->self_ticks());
}


TEST(ProfileTreeFilteredClone) {
  ProfileTree source_tree;
  const int token0 = 0, token1 = 1, token2 = 2;
  CodeEntry entry1(i::Logger::FUNCTION_TAG, "aaa", token0);
  CodeEntry entry2(i::Logger::FUNCTION_TAG, "bbb", token1);
  CodeEntry entry3(i::Logger::FUNCTION_TAG, "ccc", token0);
  CodeEntry entry4(i::Logger::FUNCTION_TAG, "ddd",
                   TokenEnumerator::kInheritsSecurityToken);

  {
    CodeEntry* e1_e2_path[] = {&entry1, &entry2};
    Vector<CodeEntry*> e1_e2_path_vec(
        e1_e2_path, sizeof(e1_e2_path) / sizeof(e1_e2_path[0]));
    source_tree.AddPathFromStart(e1_e2_path_vec);
    CodeEntry* e2_e4_path[] = {&entry2, &entry4};
    Vector<CodeEntry*> e2_e4_path_vec(
        e2_e4_path, sizeof(e2_e4_path) / sizeof(e2_e4_path[0]));
    source_tree.AddPathFromStart(e2_e4_path_vec);
    CodeEntry* e3_e1_path[] = {&entry3, &entry1};
    Vector<CodeEntry*> e3_e1_path_vec(
        e3_e1_path, sizeof(e3_e1_path) / sizeof(e3_e1_path[0]));
    source_tree.AddPathFromStart(e3_e1_path_vec);
    CodeEntry* e3_e2_path[] = {&entry3, &entry2};
    Vector<CodeEntry*> e3_e2_path_vec(
        e3_e2_path, sizeof(e3_e2_path) / sizeof(e3_e2_path[0]));
    source_tree.AddPathFromStart(e3_e2_path_vec);
    source_tree.CalculateTotalTicks();
    // Results in               -> {entry1,0,1,0} -> {entry2,1,1,1}
    //            {root,0,4,-1} -> {entry2,0,1,1} -> {entry4,1,1,inherits}
    //                          -> {entry3,0,2,0} -> {entry1,1,1,0}
    //                                            -> {entry2,1,1,1}
    CHECK_EQ(4, source_tree.root()->total_ticks());
    CHECK_EQ(0, source_tree.root()->self_ticks());
  }

  {
    ProfileTree token0_tree;
    token0_tree.FilteredClone(&source_tree, token0);
    // Should be                -> {entry1,1,1,0}
    //            {root,1,4,-1} -> {entry3,1,2,0} -> {entry1,1,1,0}
    // [self ticks from filtered nodes are attributed to their parents]
    CHECK_EQ(4, token0_tree.root()->total_ticks());
    CHECK_EQ(1, token0_tree.root()->self_ticks());
    ProfileTreeTestHelper token0_helper(&token0_tree);
    ProfileNode* node1 = token0_helper.Walk(&entry1);
    CHECK_NE(NULL, node1);
    CHECK_EQ(1, node1->total_ticks());
    CHECK_EQ(1, node1->self_ticks());
    CHECK_EQ(NULL, token0_helper.Walk(&entry2));
    ProfileNode* node3 = token0_helper.Walk(&entry3);
    CHECK_NE(NULL, node3);
    CHECK_EQ(2, node3->total_ticks());
    CHECK_EQ(1, node3->self_ticks());
    ProfileNode* node3_1 = token0_helper.Walk(&entry3, &entry1);
    CHECK_NE(NULL, node3_1);
    CHECK_EQ(1, node3_1->total_ticks());
    CHECK_EQ(1, node3_1->self_ticks());
    CHECK_EQ(NULL, token0_helper.Walk(&entry3, &entry2));
  }

  {
    ProfileTree token1_tree;
    token1_tree.FilteredClone(&source_tree, token1);
    // Should be
    //            {root,1,4,-1} -> {entry2,2,3,1} -> {entry4,1,1,inherits}
    // [child nodes referring to the same entry get merged and
    //  their self times summed up]
    CHECK_EQ(4, token1_tree.root()->total_ticks());
    CHECK_EQ(1, token1_tree.root()->self_ticks());
    ProfileTreeTestHelper token1_helper(&token1_tree);
    CHECK_EQ(NULL, token1_helper.Walk(&entry1));
    CHECK_EQ(NULL, token1_helper.Walk(&entry3));
    ProfileNode* node2 = token1_helper.Walk(&entry2);
    CHECK_NE(NULL, node2);
    CHECK_EQ(3, node2->total_ticks());
    CHECK_EQ(2, node2->self_ticks());
    ProfileNode* node2_4 = token1_helper.Walk(&entry2, &entry4);
    CHECK_NE(NULL, node2_4);
    CHECK_EQ(1, node2_4->total_ticks());
    CHECK_EQ(1, node2_4->self_ticks());
  }

  {
    ProfileTree token2_tree;
    token2_tree.FilteredClone(&source_tree, token2);
    // Should be
    //            {root,4,4,-1}
    // [no nodes, all ticks get migrated into root node]
    CHECK_EQ(4, token2_tree.root()->total_ticks());
    CHECK_EQ(4, token2_tree.root()->self_ticks());
    ProfileTreeTestHelper token2_helper(&token2_tree);
    CHECK_EQ(NULL, token2_helper.Walk(&entry1));
    CHECK_EQ(NULL, token2_helper.Walk(&entry2));
    CHECK_EQ(NULL, token2_helper.Walk(&entry3));
  }
}


static inline i::Address ToAddress(int n) {
  return reinterpret_cast<i::Address>(n);
}

TEST(CodeMapAddCode) {
  CodeMap code_map;
  CodeEntry entry1(i::Logger::FUNCTION_TAG, "aaa");
  CodeEntry entry2(i::Logger::FUNCTION_TAG, "bbb");
  CodeEntry entry3(i::Logger::FUNCTION_TAG, "ccc");
  CodeEntry entry4(i::Logger::FUNCTION_TAG, "ddd");
  code_map.AddCode(ToAddress(0x1500), &entry1, 0x200);
  code_map.AddCode(ToAddress(0x1700), &entry2, 0x100);
  code_map.AddCode(ToAddress(0x1900), &entry3, 0x50);
  code_map.AddCode(ToAddress(0x1950), &entry4, 0x10);
  CHECK_EQ(NULL, code_map.FindEntry(0));
  CHECK_EQ(NULL, code_map.FindEntry(ToAddress(0x1500 - 1)));
  CHECK_EQ(&entry1, code_map.FindEntry(ToAddress(0x1500)));
  CHECK_EQ(&entry1, code_map.FindEntry(ToAddress(0x1500 + 0x100)));
  CHECK_EQ(&entry1, code_map.FindEntry(ToAddress(0x1500 + 0x200 - 1)));
  CHECK_EQ(&entry2, code_map.FindEntry(ToAddress(0x1700)));
  CHECK_EQ(&entry2, code_map.FindEntry(ToAddress(0x1700 + 0x50)));
  CHECK_EQ(&entry2, code_map.FindEntry(ToAddress(0x1700 + 0x100 - 1)));
  CHECK_EQ(NULL, code_map.FindEntry(ToAddress(0x1700 + 0x100)));
  CHECK_EQ(NULL, code_map.FindEntry(ToAddress(0x1900 - 1)));
  CHECK_EQ(&entry3, code_map.FindEntry(ToAddress(0x1900)));
  CHECK_EQ(&entry3, code_map.FindEntry(ToAddress(0x1900 + 0x28)));
  CHECK_EQ(&entry4, code_map.FindEntry(ToAddress(0x1950)));
  CHECK_EQ(&entry4, code_map.FindEntry(ToAddress(0x1950 + 0x7)));
  CHECK_EQ(&entry4, code_map.FindEntry(ToAddress(0x1950 + 0x10 - 1)));
  CHECK_EQ(NULL, code_map.FindEntry(ToAddress(0x1950 + 0x10)));
  CHECK_EQ(NULL, code_map.FindEntry(ToAddress(0xFFFFFFFF)));
}


TEST(CodeMapMoveAndDeleteCode) {
  CodeMap code_map;
  CodeEntry entry1(i::Logger::FUNCTION_TAG, "aaa");
  CodeEntry entry2(i::Logger::FUNCTION_TAG, "bbb");
  code_map.AddCode(ToAddress(0x1500), &entry1, 0x200);
  code_map.AddCode(ToAddress(0x1700), &entry2, 0x100);
  CHECK_EQ(&entry1, code_map.FindEntry(ToAddress(0x1500)));
  CHECK_EQ(&entry2, code_map.FindEntry(ToAddress(0x1700)));
  code_map.MoveCode(ToAddress(0x1500), ToAddress(0x1700));  // Deprecate bbb.
  CHECK_EQ(NULL, code_map.FindEntry(ToAddress(0x1500)));
  CHECK_EQ(&entry1, code_map.FindEntry(ToAddress(0x1700)));
  CodeEntry entry3(i::Logger::FUNCTION_TAG, "ccc");
  code_map.AddCode(ToAddress(0x1750), &entry3, 0x100);
  CHECK_EQ(NULL, code_map.FindEntry(ToAddress(0x1700)));
  CHECK_EQ(&entry3, code_map.FindEntry(ToAddress(0x1750)));
}


namespace {

class TestSetup {
 public:
  TestSetup()
      : old_flag_prof_browser_mode_(i::FLAG_prof_browser_mode) {
    i::FLAG_prof_browser_mode = false;
  }

  ~TestSetup() {
    i::FLAG_prof_browser_mode = old_flag_prof_browser_mode_;
  }

 private:
  bool old_flag_prof_browser_mode_;
};

}  // namespace

TEST(RecordTickSample) {
  TestSetup test_setup;
  CpuProfilesCollection profiles;
  profiles.StartProfiling("", 1, false);
  ProfileGenerator generator(&profiles);
  CodeEntry* entry1 = profiles.NewCodeEntry(i::Logger::FUNCTION_TAG, "aaa");
  CodeEntry* entry2 = profiles.NewCodeEntry(i::Logger::FUNCTION_TAG, "bbb");
  CodeEntry* entry3 = profiles.NewCodeEntry(i::Logger::FUNCTION_TAG, "ccc");
  generator.code_map()->AddCode(ToAddress(0x1500), entry1, 0x200);
  generator.code_map()->AddCode(ToAddress(0x1700), entry2, 0x100);
  generator.code_map()->AddCode(ToAddress(0x1900), entry3, 0x50);

  // We are building the following calls tree:
  //      -> aaa         - sample1
  //  aaa -> bbb -> ccc  - sample2
  //      -> ccc -> aaa  - sample3
  TickSample sample1;
  sample1.pc = ToAddress(0x1600);
  sample1.tos = ToAddress(0x1500);
  sample1.stack[0] = ToAddress(0x1510);
  sample1.frames_count = 1;
  generator.RecordTickSample(sample1);
  TickSample sample2;
  sample2.pc = ToAddress(0x1925);
  sample2.tos = ToAddress(0x1900);
  sample2.stack[0] = ToAddress(0x1780);
  sample2.stack[1] = ToAddress(0x10000);  // non-existent.
  sample2.stack[2] = ToAddress(0x1620);
  sample2.frames_count = 3;
  generator.RecordTickSample(sample2);
  TickSample sample3;
  sample3.pc = ToAddress(0x1510);
  sample3.tos = ToAddress(0x1500);
  sample3.stack[0] = ToAddress(0x1910);
  sample3.stack[1] = ToAddress(0x1610);
  sample3.frames_count = 2;
  generator.RecordTickSample(sample3);

  CpuProfile* profile =
      profiles.StopProfiling(TokenEnumerator::kNoSecurityToken, "", 1);
  CHECK_NE(NULL, profile);
  ProfileTreeTestHelper top_down_test_helper(profile->top_down());
  CHECK_EQ(NULL, top_down_test_helper.Walk(entry2));
  CHECK_EQ(NULL, top_down_test_helper.Walk(entry3));
  ProfileNode* node1 = top_down_test_helper.Walk(entry1);
  CHECK_NE(NULL, node1);
  CHECK_EQ(entry1, node1->entry());
  ProfileNode* node2 = top_down_test_helper.Walk(entry1, entry1);
  CHECK_NE(NULL, node2);
  CHECK_EQ(entry1, node2->entry());
  ProfileNode* node3 = top_down_test_helper.Walk(entry1, entry2, entry3);
  CHECK_NE(NULL, node3);
  CHECK_EQ(entry3, node3->entry());
  ProfileNode* node4 = top_down_test_helper.Walk(entry1, entry3, entry1);
  CHECK_NE(NULL, node4);
  CHECK_EQ(entry1, node4->entry());
}


TEST(SampleRateCalculator) {
  const double kSamplingIntervalMs = i::Logger::kSamplingIntervalMs;

  // Verify that ticking exactly in query intervals results in the
  // initial sampling interval.
  double time = 0.0;
  SampleRateCalculator calc1;
  CHECK_EQ(kSamplingIntervalMs, calc1.ticks_per_ms());
  calc1.UpdateMeasurements(time);
  CHECK_EQ(kSamplingIntervalMs, calc1.ticks_per_ms());
  time += SampleRateCalculator::kWallTimeQueryIntervalMs;
  calc1.UpdateMeasurements(time);
  CHECK_EQ(kSamplingIntervalMs, calc1.ticks_per_ms());
  time += SampleRateCalculator::kWallTimeQueryIntervalMs;
  calc1.UpdateMeasurements(time);
  CHECK_EQ(kSamplingIntervalMs, calc1.ticks_per_ms());
  time += SampleRateCalculator::kWallTimeQueryIntervalMs;
  calc1.UpdateMeasurements(time);
  CHECK_EQ(kSamplingIntervalMs, calc1.ticks_per_ms());

  SampleRateCalculator calc2;
  time = 0.0;
  CHECK_EQ(kSamplingIntervalMs, calc2.ticks_per_ms());
  calc2.UpdateMeasurements(time);
  CHECK_EQ(kSamplingIntervalMs, calc2.ticks_per_ms());
  time += SampleRateCalculator::kWallTimeQueryIntervalMs * 0.5;
  calc2.UpdateMeasurements(time);
  // (1.0 + 2.0) / 2
  CHECK_EQ(kSamplingIntervalMs * 1.5, calc2.ticks_per_ms());
  time += SampleRateCalculator::kWallTimeQueryIntervalMs * 0.75;
  calc2.UpdateMeasurements(time);
  // (1.0 + 2.0 + 2.0) / 3
  CHECK_EQ(kSamplingIntervalMs * 5.0, floor(calc2.ticks_per_ms() * 3.0 + 0.5));

  SampleRateCalculator calc3;
  time = 0.0;
  CHECK_EQ(kSamplingIntervalMs, calc3.ticks_per_ms());
  calc3.UpdateMeasurements(time);
  CHECK_EQ(kSamplingIntervalMs, calc3.ticks_per_ms());
  time += SampleRateCalculator::kWallTimeQueryIntervalMs * 2;
  calc3.UpdateMeasurements(time);
  // (1.0 + 0.5) / 2
  CHECK_EQ(kSamplingIntervalMs * 0.75, calc3.ticks_per_ms());
  time += SampleRateCalculator::kWallTimeQueryIntervalMs * 1.5;
  calc3.UpdateMeasurements(time);
  // (1.0 + 0.5 + 0.5) / 3
  CHECK_EQ(kSamplingIntervalMs * 2.0, floor(calc3.ticks_per_ms() * 3.0 + 0.5));
}


static void CheckNodeIds(ProfileNode* node, int* expectedId) {
  CHECK_EQ((*expectedId)++, node->id());
  for (int i = 0; i < node->children()->length(); i++) {
    CheckNodeIds(node->children()->at(i), expectedId);
  }
}

TEST(SampleIds) {
  TestSetup test_setup;
  CpuProfilesCollection profiles;
  profiles.StartProfiling("", 1, true);
  ProfileGenerator generator(&profiles);
  CodeEntry* entry1 = profiles.NewCodeEntry(i::Logger::FUNCTION_TAG, "aaa");
  CodeEntry* entry2 = profiles.NewCodeEntry(i::Logger::FUNCTION_TAG, "bbb");
  CodeEntry* entry3 = profiles.NewCodeEntry(i::Logger::FUNCTION_TAG, "ccc");
  generator.code_map()->AddCode(ToAddress(0x1500), entry1, 0x200);
  generator.code_map()->AddCode(ToAddress(0x1700), entry2, 0x100);
  generator.code_map()->AddCode(ToAddress(0x1900), entry3, 0x50);

  // We are building the following calls tree:
  //                    -> aaa #3           - sample1
  // (root)#1 -> aaa #2 -> bbb #4 -> ccc #5 - sample2
  //                    -> ccc #6 -> aaa #7 - sample3
  TickSample sample1;
  sample1.pc = ToAddress(0x1600);
  sample1.stack[0] = ToAddress(0x1510);
  sample1.frames_count = 1;
  generator.RecordTickSample(sample1);
  TickSample sample2;
  sample2.pc = ToAddress(0x1925);
  sample2.stack[0] = ToAddress(0x1780);
  sample2.stack[1] = ToAddress(0x10000);  // non-existent.
  sample2.stack[2] = ToAddress(0x1620);
  sample2.frames_count = 3;
  generator.RecordTickSample(sample2);
  TickSample sample3;
  sample3.pc = ToAddress(0x1510);
  sample3.stack[0] = ToAddress(0x1910);
  sample3.stack[1] = ToAddress(0x1610);
  sample3.frames_count = 2;
  generator.RecordTickSample(sample3);

  CpuProfile* profile =
      profiles.StopProfiling(TokenEnumerator::kNoSecurityToken, "", 1);
  int nodeId = 1;
  CheckNodeIds(profile->top_down()->root(), &nodeId);
  CHECK_EQ(7, nodeId - 1);

  CHECK_EQ(3, profile->samples_count());
  int expected_id[] = {3, 5, 7};
  for (int i = 0; i < 3; i++) {
    CHECK_EQ(expected_id[i], profile->sample(i)->id());
  }
}


TEST(NoSamples) {
  TestSetup test_setup;
  CpuProfilesCollection profiles;
  profiles.StartProfiling("", 1, false);
  ProfileGenerator generator(&profiles);
  CodeEntry* entry1 = profiles.NewCodeEntry(i::Logger::FUNCTION_TAG, "aaa");
  generator.code_map()->AddCode(ToAddress(0x1500), entry1, 0x200);

  // We are building the following calls tree:
  // (root)#1 -> aaa #2 -> aaa #3 - sample1
  TickSample sample1;
  sample1.pc = ToAddress(0x1600);
  sample1.stack[0] = ToAddress(0x1510);
  sample1.frames_count = 1;
  generator.RecordTickSample(sample1);

  CpuProfile* profile =
      profiles.StopProfiling(TokenEnumerator::kNoSecurityToken, "", 1);
  int nodeId = 1;
  CheckNodeIds(profile->top_down()->root(), &nodeId);
  CHECK_EQ(3, nodeId - 1);

  CHECK_EQ(0, profile->samples_count());
}


// --- P r o f i l e r   E x t e n s i o n ---

class ProfilerExtension : public v8::Extension {
 public:
  ProfilerExtension() : v8::Extension("v8/profiler", kSource) { }
  virtual v8::Handle<v8::FunctionTemplate> GetNativeFunction(
      v8::Handle<v8::String> name);
  static void StartProfiling(const v8::FunctionCallbackInfo<v8::Value>& args);
  static void StopProfiling(const v8::FunctionCallbackInfo<v8::Value>& args);
 private:
  static const char* kSource;
};


const char* ProfilerExtension::kSource =
    "native function startProfiling();"
    "native function stopProfiling();";

v8::Handle<v8::FunctionTemplate> ProfilerExtension::GetNativeFunction(
    v8::Handle<v8::String> name) {
  if (name->Equals(v8::String::New("startProfiling"))) {
    return v8::FunctionTemplate::New(ProfilerExtension::StartProfiling);
  } else if (name->Equals(v8::String::New("stopProfiling"))) {
    return v8::FunctionTemplate::New(ProfilerExtension::StopProfiling);
  } else {
    CHECK(false);
    return v8::Handle<v8::FunctionTemplate>();
  }
}


void ProfilerExtension::StartProfiling(
    const v8::FunctionCallbackInfo<v8::Value>& args) {
  v8::CpuProfiler* cpu_profiler = args.GetIsolate()->GetCpuProfiler();
  if (args.Length() > 0)
    cpu_profiler->StartCpuProfiling(args[0].As<v8::String>());
  else
    cpu_profiler->StartCpuProfiling(v8::String::New(""));
}


void ProfilerExtension::StopProfiling(
    const v8::FunctionCallbackInfo<v8::Value>& args) {
  v8::CpuProfiler* cpu_profiler = args.GetIsolate()->GetCpuProfiler();
  if (args.Length() > 0)
    cpu_profiler->StopCpuProfiling(args[0].As<v8::String>());
  else
    cpu_profiler->StopCpuProfiling(v8::String::New(""));
}


static ProfilerExtension kProfilerExtension;
v8::DeclareExtension kProfilerExtensionDeclaration(&kProfilerExtension);

static const ProfileNode* PickChild(const ProfileNode* parent,
                                    const char* name) {
  for (int i = 0; i < parent->children()->length(); ++i) {
    const ProfileNode* child = parent->children()->at(i);
    if (strcmp(child->entry()->name(), name) == 0) return child;
  }
  return NULL;
}


TEST(RecordStackTraceAtStartProfiling) {
  // This test does not pass with inlining enabled since inlined functions
  // don't appear in the stack trace.
  i::FLAG_use_inlining = false;

  v8::Isolate* isolate = v8::Isolate::GetCurrent();
  v8::HandleScope scope(isolate);
  const char* extensions[] = { "v8/profiler" };
  v8::ExtensionConfiguration config(1, extensions);
  v8::Local<v8::Context> context = v8::Context::New(isolate, &config);
  context->Enter();

  CpuProfiler* profiler = i::Isolate::Current()->cpu_profiler();
  CHECK_EQ(0, profiler->GetProfilesCount());
  CompileRun(
      "function c() { startProfiling(); }\n"
      "function b() { c(); }\n"
      "function a() { b(); }\n"
      "a();\n"
      "stopProfiling();");
  CHECK_EQ(1, profiler->GetProfilesCount());
  CpuProfile* profile = profiler->GetProfile(NULL, 0);
  const ProfileTree* topDown = profile->top_down();
  const ProfileNode* current = topDown->root();
  const_cast<ProfileNode*>(current)->Print(0);
  // The tree should look like this:
  //  (root)
  //   (anonymous function)
  //     a
  //       b
  //         c
  // There can also be:
  //           startProfiling
  // if the sampler managed to get a tick.
  current = PickChild(current, "(anonymous function)");
  CHECK_NE(NULL, const_cast<ProfileNode*>(current));
  current = PickChild(current, "a");
  CHECK_NE(NULL, const_cast<ProfileNode*>(current));
  current = PickChild(current, "b");
  CHECK_NE(NULL, const_cast<ProfileNode*>(current));
  current = PickChild(current, "c");
  CHECK_NE(NULL, const_cast<ProfileNode*>(current));
  CHECK(current->children()->length() == 0 ||
        current->children()->length() == 1);
  if (current->children()->length() == 1) {
    current = PickChild(current, "startProfiling");
    CHECK_EQ(0, current->children()->length());
  }
}


TEST(Issue51919) {
  CpuProfilesCollection collection;
  i::EmbeddedVector<char*,
      CpuProfilesCollection::kMaxSimultaneousProfiles> titles;
  for (int i = 0; i < CpuProfilesCollection::kMaxSimultaneousProfiles; ++i) {
    i::Vector<char> title = i::Vector<char>::New(16);
    i::OS::SNPrintF(title, "%d", i);
    // UID must be > 0.
    CHECK(collection.StartProfiling(title.start(), i + 1, false));
    titles[i] = title.start();
  }
  CHECK(!collection.StartProfiling(
      "maximum", CpuProfilesCollection::kMaxSimultaneousProfiles + 1, false));
  for (int i = 0; i < CpuProfilesCollection::kMaxSimultaneousProfiles; ++i)
    i::DeleteArray(titles[i]);
}
