/*
 * Copyright 2014 Google Inc.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

// Author: stevensr@google.com (Ryan Stevens)

#include "net/instaweb/rewriter/public/mobilize_rewrite_filter.h"

#include "base/logging.h"
#include "net/instaweb/public/global_constants.h"
#include "net/instaweb/rewriter/public/rewrite_driver.h"
#include "net/instaweb/rewriter/public/rewrite_options.h"
#include "net/instaweb/rewriter/public/rewrite_test_base.h"
#include "net/instaweb/rewriter/public/server_context.h"
#include "pagespeed/kernel/base/gtest.h"
#include "pagespeed/kernel/base/mock_message_handler.h"
#include "pagespeed/kernel/base/scoped_ptr.h"
#include "pagespeed/kernel/base/statistics.h"
#include "pagespeed/kernel/base/stdio_file_system.h"
#include "pagespeed/kernel/base/string.h"
#include "pagespeed/kernel/base/string_util.h"
#include "pagespeed/kernel/html/html_element.h"
#include "pagespeed/kernel/html/html_node.h"
#include "pagespeed/kernel/html/html_parse_test_base.h"
#include "pagespeed/kernel/html/html_writer_filter.h"
#include "pagespeed/kernel/http/content_type.h"
#include "pagespeed/kernel/http/user_agent_matcher_test_base.h"

namespace net_instaweb {

namespace {

const char kTestDataDir[] = "/net/instaweb/rewriter/testdata/";
const char kOriginal[] = "mobilize_test.html";
const char kRewritten[] = "mobilize_test_output.html";
const char kPhoneNumber[] = "16175551212";
const int64 kConversionId = 42;
const char kPhoneConversionLabel[] = "HelloWorld";
const char kMobBeaconUrl[] = "/beacon";

GoogleString Styles(bool layout_mode) {
  return StrCat(
      "<link rel=\"stylesheet\" href=\"/psajs/mobilize_css.0.css\">",
      (layout_mode
       ? "<link rel=\"stylesheet\" href=\"/psajs/mobilize_layout_css.0.css\">"
       : ""));
}

GoogleString HeadAndViewport(bool layout_mode) {
  return StrCat(
      "<meta itemprop=\"telephone\" content=\"", kPhoneNumber, "\"/>",
      (layout_mode ? ("<meta name='viewport' content='width=device-width'/>"
                      "<script src=\"/psajs/mobilize_xhr.0.js\"></script>")
                   : ""));
}

}  // namespace

// Base class for doing our tests. Can access MobilizeRewriteFilter's private
// API.
class MobilizeRewriteFilterTest : public RewriteTestBase {
 protected:
  MobilizeRewriteFilterTest() {}

  void CheckExpected(const GoogleString& expected) {
    PrepareWrite();
    EXPECT_STREQ(expected, output_buffer_);
  }

  virtual void SetUp() {
    RewriteTestBase::SetUp();
    options()->ClearSignatureForTesting();
    options()->set_mob_always(true);
    options()->set_mob_phone_number(kPhoneNumber);
    options()->set_mob_conversion_id(kConversionId);
    options()->set_mob_phone_conversion_label(kPhoneConversionLabel);
    options()->set_mob_beacon_url(kMobBeaconUrl);
    options()->set_mob_layout(LayoutMode());
    options()->set_mob_nav(true);
    server_context()->ComputeSignature(options());
    SetHtmlMimetype();  // Don't wrap scripts in <![CDATA[ ]]>

    filter_.reset(new MobilizeRewriteFilter(rewrite_driver()));
  }

  virtual void TearDown() {
    RewriteTestBase::TearDown();
  }

  virtual bool LayoutMode() const { return true; }
  virtual bool AddBody() const { return false; }
  virtual bool AddHtmlTags() const { return false; }

  void CheckVariable(const char* name, int value) {
    Variable* var = rewrite_driver()->statistics()->FindVariable(name);
    if (var == NULL) {
      CHECK(false) << "Checked for a variable that doesn't exit.";
    } else {
      EXPECT_EQ(value, var->Get()) << name;
    }
  }

  // Wrappers for MobilizeRewriteFilter private API.
  void FilterAddStyle(HtmlElement* element) {
    filter_->AddStyle(element);
  }
  MobileRole::Level FilterGetMobileRole(HtmlElement* element) {
    return filter_->GetMobileRole(element);
  }
  void FilterSetAddedProgress(bool added) {
    filter_->added_progress_ = added;
  }

  GoogleString ScriptsAtEndOfBody(StringPiece bg_color,
                                  StringPiece fg_color) const {
    return StrCat(
        "<script src=\"/psajs/mobilize.0.js\"></script>"
        "<script>window.psDebugMode=false;window.psNavMode=true;"
        "window.psLabeledMode=false;window.psConfigMode=false;"
        "window.psLayoutMode=",
        BoolToString(LayoutMode()),
        ";window.psStaticJs=false;"
        "window.psDeviceType='mobile';",
        "window.psConversionId='", Integer64ToString(kConversionId),
        "';window.psPhoneNumber='", kPhoneNumber,
        "';window.psPhoneConversionLabel='", kPhoneConversionLabel,
        "';window.psMobBackgroundColor=", bg_color,
        ";window.psMobForegroundColor=", fg_color, ";window.psMobBeaconUrl='",
        kMobBeaconUrl, "';psStartMobilization();</script>");
  }
  GoogleString ScriptsAtEndOfBody() const {
    return ScriptsAtEndOfBody("null", "null");
  }

  GoogleString Spacer() const {
    return "<header id=\"psmob-header-bar\" class=\"psmob-hide\"></header>"
           "<div id=\"psmob-spacer\"></div>";
  }

  GoogleString Scrim() const {
    return
        "<div id=\"ps-progress-scrim\" class=\"psProgressScrim\">"
        "<a href=\"javascript:psRemoveProgressBar();\""
        " id=\"ps-progress-remove\" id=\"ps-progress-show-log\">"
        "Remove Progress Bar (doesn't stop mobilization)</a><br>"
        "<a href=\"javascript:psSetDebugMode();\">"
        "Show Debug Log In Progress Bar</a>"
        "<div class=\"psProgressBar\">"
        "<span id=\"ps-progress-span\" class=\"psProgressSpan\"></span></div>"
        "<pre id=\"ps-progress-log\" class=\"psProgressLog\"/></div>";
  }

  scoped_ptr<MobilizeRewriteFilter> filter_;

 private:
  void PrepareWrite() {
    SetupWriter();
    html_parse()->ApplyFilter(html_writer_filter_.get());
  }

  DISALLOW_COPY_AND_ASSIGN(MobilizeRewriteFilterTest);
};

namespace {

// For testing private functions in isolation.
class MobilizeRewriteUnitTest : public MobilizeRewriteFilterTest {
 protected:
  MobilizeRewriteUnitTest() {}

  virtual void SetUp() {
    MobilizeRewriteFilterTest::SetUp();
    static const char kUrl[] = "http://mob.rewrite.test/test.html";
    ASSERT_TRUE(html_parse()->StartParse(kUrl));
  }

  virtual void TearDown() {
    html_parse()->FinishParse();
    MobilizeRewriteFilterTest::TearDown();
  }

 private:
  DISALLOW_COPY_AND_ASSIGN(MobilizeRewriteUnitTest);
};

TEST_F(MobilizeRewriteUnitTest, AddStyle) {
  HtmlElement* head = html_parse()->NewElement(NULL, HtmlName::kHead);
  html_parse()->InsertNodeBeforeCurrent(head);
  HtmlCharactersNode* content = html_parse()->NewCharactersNode(head, "123");
  html_parse()->AppendChild(head, content);
  CheckExpected("<head>123</head>");
  FilterAddStyle(head);
  CheckExpected(StrCat("<head>123", Styles(LayoutMode()), "</head>"));
}

TEST_F(MobilizeRewriteUnitTest, MobileRoleAttribute) {
  HtmlElement* div = html_parse()->NewElement(NULL, HtmlName::kDiv);
  html_parse()->AddAttribute(div, "data-mobile-role", "navigational");
  // Add the new node to the parse tree so it will be deleted.
  html_parse()->InsertNodeBeforeCurrent(div);
  EXPECT_EQ(MobileRole::kNavigational,
            FilterGetMobileRole(div));
}

TEST_F(MobilizeRewriteUnitTest, InvalidMobileRoleAttribute) {
  HtmlElement* div = html_parse()->NewElement(NULL, HtmlName::kDiv);
  html_parse()->AddAttribute(div, "data-mobile-role", "garbage");
  // Add the new node to the parse tree so it will be deleted.
  html_parse()->InsertNodeBeforeCurrent(div);
  EXPECT_EQ(MobileRole::kInvalid,
            FilterGetMobileRole(div));
}

TEST_F(MobilizeRewriteUnitTest, KeeperMobileRoleAttribute) {
  HtmlElement* script = html_parse()->NewElement(NULL, HtmlName::kScript);
  // Add the new node to the parse tree so it will be deleted.
  html_parse()->InsertNodeBeforeCurrent(script);
  EXPECT_EQ(MobileRole::kKeeper,
            FilterGetMobileRole(script));
}

class MobilizeRewriteFunctionalTest : public MobilizeRewriteFilterTest {
 protected:
  MobilizeRewriteFunctionalTest() {}

  virtual void SetUp() {
    MobilizeRewriteFilterTest::SetUp();
    rewrite_driver()->AppendUnownedPreRenderFilter(filter_.get());
    // By default we *don't* add the progress bar scrim.  This explicitly gets
    // overridden in subclasses.
    FilterSetAddedProgress(true);
    SetCurrentUserAgent(
        UserAgentMatcherTestBase::kAndroidChrome21UserAgent);
  }

  void HeadTest(const char* name,
                StringPiece original_head, StringPiece expected_mid_head,
                int deleted_elements, int keeper_blocks) {
    GoogleString original =
        StrCat("<head>\n", original_head, "\n</head>", Body());
    GoogleString expected =
        StrCat("<head>", HeadAndViewport(LayoutMode()), "\n", expected_mid_head,
               "\n", Styles(LayoutMode()), "</head>", ExpectedBody());
    ValidateExpected(name, original, expected);
    CheckVariable(MobilizeRewriteFilter::kPagesMobilized, 1);
    CheckVariable(MobilizeRewriteFilter::kKeeperBlocks, keeper_blocks);
    CheckVariable(MobilizeRewriteFilter::kHeaderBlocks, 0);
    CheckVariable(MobilizeRewriteFilter::kNavigationalBlocks, 0);
    CheckVariable(MobilizeRewriteFilter::kContentBlocks, 0);
    CheckVariable(MobilizeRewriteFilter::kMarginalBlocks, 0);
    CheckVariable(MobilizeRewriteFilter::kDeletedElements, deleted_elements);
  }

  void BodyTest(const char* name,
                StringPiece original_body, StringPiece expected_mid_body) {
    // TODO(jmaessen): We should inject a head in these cases, possibly by
    // requiring AddHeadFilter to run.  We should also deal with the complete
    // absence of a body tag.
    GoogleString original =
        StrCat("\n<body>\n", original_body, "\n</body>\n");
    GoogleString expected =
        StrCat("\n<body>", Spacer(), "\n", expected_mid_body, "\n",
               ScriptsAtEndOfBody(), "</body>\n");
    ValidateExpected(name, original, expected);
    CheckVariable(MobilizeRewriteFilter::kPagesMobilized, 1);
  }

  void BodyUnchanged(const char* name, StringPiece body) {
    BodyTest(name, body, body);
  }

  void KeeperTagsTest(const char* name, GoogleString keeper) {
    BodyUnchanged(name, keeper);
    CheckVariable(MobilizeRewriteFilter::kKeeperBlocks, 1);
    CheckVariable(MobilizeRewriteFilter::kHeaderBlocks, 0);
    CheckVariable(MobilizeRewriteFilter::kNavigationalBlocks, 0);
    CheckVariable(MobilizeRewriteFilter::kContentBlocks, 0);
    CheckVariable(MobilizeRewriteFilter::kMarginalBlocks, 0);
    CheckVariable(MobilizeRewriteFilter::kDeletedElements, 0);
  }

  void TwoBodysTest(const char* name,
                    StringPiece first_body, StringPiece second_body) {
    GoogleString original =
        StrCat("\n<body>\n", first_body,
               "\n</body>\n<body>\n", second_body, "\n</body>\n");
    GoogleString expected =
        StrCat("\n<body>", Spacer(), "\n", first_body, "\n</body>\n<body>\n",
               second_body, "\n", ScriptsAtEndOfBody(), "</body>\n");
    ValidateExpected(name, original, expected);
    CheckVariable(MobilizeRewriteFilter::kPagesMobilized, 1);
  }

  GoogleString Body() const {
    return "\n<body>\nhello, world!\n</body>\n";
  }

  GoogleString ExpectedBody() const {
    return StrCat("\n<body>", Spacer(), "\nhello, world!\n",
                  ScriptsAtEndOfBody(), "</body>\n");
  }

  GoogleString ExpectedBody(StringPiece bg_color, StringPiece fg_color) const {
    return StrCat("\n<body>", Spacer(), "\nhello, world!\n",
                  ScriptsAtEndOfBody(bg_color, fg_color), "</body>\n");
  }

 private:
  DISALLOW_COPY_AND_ASSIGN(MobilizeRewriteFunctionalTest);
};

TEST_F(MobilizeRewriteFunctionalTest, AddStyleAndViewport) {
  HeadTest("add_style_and_viewport", "", "", 0, 0);
}

TEST_F(MobilizeRewriteFunctionalTest, RemoveExistingViewport) {
  HeadTest("remove_existing_viewport",
           "<meta name='viewport' content='value' />", "", 1, 0);
}

TEST_F(MobilizeRewriteFunctionalTest, RemoveExistingViewportThatMatches) {
  HeadTest("remove_existing_viewport",
           "<meta name='viewport' content='width=device-width'/>", "", 1, 0);
}

TEST_F(MobilizeRewriteFunctionalTest, HeadUnmodified) {
  const char kHeadTags[] =
      "<meta name='keywords' content='cool,stuff'/>"
      "<style>abcd</style>";
  HeadTest("head_unmodified", kHeadTags, kHeadTags, 0, 1);
}

TEST_F(MobilizeRewriteFunctionalTest, HeadLinksUnmodified) {
  const char kLink[] =
      "<link rel='stylesheet' type='text/css' href='theme.css'>";
  HeadTest("head_unmodified", kLink, kLink, 0, 1);
}

TEST_F(MobilizeRewriteFunctionalTest, EmptyBody) {
  GoogleString expected = StrCat("<body>", Spacer(), ScriptsAtEndOfBody(),
                                 "</body>");
  ValidateExpected("empty_body",
                   "<body></body>", expected);
  CheckVariable(MobilizeRewriteFilter::kPagesMobilized, 1);
  CheckVariable(MobilizeRewriteFilter::kKeeperBlocks, 0);
  CheckVariable(MobilizeRewriteFilter::kHeaderBlocks, 0);
  CheckVariable(MobilizeRewriteFilter::kNavigationalBlocks, 0);
  CheckVariable(MobilizeRewriteFilter::kContentBlocks, 0);
  CheckVariable(MobilizeRewriteFilter::kMarginalBlocks, 0);
  CheckVariable(MobilizeRewriteFilter::kDeletedElements, 0);
}

TEST_F(MobilizeRewriteFunctionalTest, EmptyBodyWithProgress) {
  FilterSetAddedProgress(false);
  GoogleString expected = StrCat(
      "<body>",
      Spacer(),
      "<div id=\"ps-progress-scrim\" class=\"psProgressScrim\">"
      "<a href=\"javascript:psRemoveProgressBar();\" id=\"ps-progress-remove\""
      " id=\"ps-progress-show-log\">Remove Progress Bar"
      " (doesn't stop mobilization)</a><br>"
      "<a href=\"javascript:psSetDebugMode();\">"
      "Show Debug Log In Progress Bar</a>"
      "<div class=\"psProgressBar\">"
      "<span id=\"ps-progress-span\" class=\"psProgressSpan\"></span>"
      "</div><pre id=\"ps-progress-log\" class=\"psProgressLog\"/></div>",
      ScriptsAtEndOfBody(), "</body>");
  ValidateExpected("empty_body_with_progress",
                   "<body></body>", expected);
  CheckVariable(MobilizeRewriteFilter::kPagesMobilized, 1);
  CheckVariable(MobilizeRewriteFilter::kKeeperBlocks, 0);
  CheckVariable(MobilizeRewriteFilter::kHeaderBlocks, 0);
  CheckVariable(MobilizeRewriteFilter::kNavigationalBlocks, 0);
  CheckVariable(MobilizeRewriteFilter::kContentBlocks, 0);
  CheckVariable(MobilizeRewriteFilter::kMarginalBlocks, 0);
  CheckVariable(MobilizeRewriteFilter::kDeletedElements, 0);
}

TEST_F(MobilizeRewriteFunctionalTest, MapTagsUnmodified) {
  KeeperTagsTest("map_tags_unmodified",
                 "<map name='planetmap'><area shape='rect'"
                 " coords='0,0,82,126' alt='Sun'></map>");
}

TEST_F(MobilizeRewriteFunctionalTest, ScriptTagsUnmodified) {
  KeeperTagsTest("script_tags_unmodified",
                 "<script>document.getElementById('demo')."
                 "innerHTML = 'Hello JavaScript!';</script>");
}

TEST_F(MobilizeRewriteFunctionalTest, StyleTagsUnmodified) {
  KeeperTagsTest("style_tags_unmodified",
                 "<style>* { foo: bar; }</style>");
}

TEST_F(MobilizeRewriteFunctionalTest, UnknownMobileRole) {
  // Its probably OK if the behavior resulting from having a weird
  // data-mobile-role value is unexpected, as long as it doesn't crash.
  BodyUnchanged(
      "unknown_mobile_role",
      "<div data-mobile-role='garbage'><a>123</a></div>");
  CheckVariable(MobilizeRewriteFilter::kKeeperBlocks, 0);
  CheckVariable(MobilizeRewriteFilter::kHeaderBlocks, 0);
  CheckVariable(MobilizeRewriteFilter::kNavigationalBlocks, 0);
  CheckVariable(MobilizeRewriteFilter::kContentBlocks, 0);
  CheckVariable(MobilizeRewriteFilter::kMarginalBlocks, 0);
  CheckVariable(MobilizeRewriteFilter::kDeletedElements, 0);
}

TEST_F(MobilizeRewriteFunctionalTest, MultipleHeads) {
  // Check we only add the style and viewport tag once.
  const char kRestOfHeads[] = "</head><head></head>";
  GoogleString original = StrCat("<head>", kRestOfHeads);
  GoogleString expected =
      StrCat("<head>", HeadAndViewport(LayoutMode()), Styles(LayoutMode()),
             kRestOfHeads, ScriptsAtEndOfBody());
  ValidateExpected("multiple_heads", original, expected);
  CheckVariable(MobilizeRewriteFilter::kPagesMobilized, 1);
  CheckVariable(MobilizeRewriteFilter::kKeeperBlocks, 0);
  CheckVariable(MobilizeRewriteFilter::kHeaderBlocks, 0);
  CheckVariable(MobilizeRewriteFilter::kNavigationalBlocks, 0);
  CheckVariable(MobilizeRewriteFilter::kContentBlocks, 0);
  CheckVariable(MobilizeRewriteFilter::kMarginalBlocks, 0);
  CheckVariable(MobilizeRewriteFilter::kDeletedElements, 0);
}

TEST_F(MobilizeRewriteFunctionalTest, MultipleBodys) {
  // Each body should be handled as its own unit.
  TwoBodysTest("multiple_bodys", "", "");
  CheckVariable(MobilizeRewriteFilter::kKeeperBlocks, 0);
  CheckVariable(MobilizeRewriteFilter::kHeaderBlocks, 0);
  CheckVariable(MobilizeRewriteFilter::kNavigationalBlocks, 0);
  CheckVariable(MobilizeRewriteFilter::kContentBlocks, 0);
  CheckVariable(MobilizeRewriteFilter::kMarginalBlocks, 0);
  CheckVariable(MobilizeRewriteFilter::kDeletedElements, 0);
}

TEST_F(MobilizeRewriteFunctionalTest, MultipleBodysWithContent) {
  TwoBodysTest(
      "multiple_bodys_with_content",
      "123<div data-mobile-role='marginal'>567</div>",
      "<div data-mobile-role='content'>890</div>"
      "<div data-mobile-role='header'>abc</div>");
  CheckVariable(MobilizeRewriteFilter::kKeeperBlocks, 0);
  CheckVariable(MobilizeRewriteFilter::kHeaderBlocks, 1);
  CheckVariable(MobilizeRewriteFilter::kNavigationalBlocks, 0);
  CheckVariable(MobilizeRewriteFilter::kContentBlocks, 1);
  CheckVariable(MobilizeRewriteFilter::kMarginalBlocks, 1);
  CheckVariable(MobilizeRewriteFilter::kDeletedElements, 0);
}

TEST_F(MobilizeRewriteFunctionalTest, HeaderWithinBody) {
  BodyUnchanged(
      "header_within_body",
      "<div data-mobile-role='content'>123<div data-mobile-role='header'>"
      "456</div>789</div>");
  CheckVariable(MobilizeRewriteFilter::kKeeperBlocks, 0);
  CheckVariable(MobilizeRewriteFilter::kHeaderBlocks, 1);
  CheckVariable(MobilizeRewriteFilter::kNavigationalBlocks, 0);
  CheckVariable(MobilizeRewriteFilter::kContentBlocks, 1);
  CheckVariable(MobilizeRewriteFilter::kMarginalBlocks, 0);
  CheckVariable(MobilizeRewriteFilter::kDeletedElements, 0);
}

TEST_F(MobilizeRewriteFunctionalTest, HeaderWithinHeader) {
  // Note: this should occur primarily as a result of a nested HTML5 tag, as the
  // labeler should not label children with the parent's label.
  BodyUnchanged(
      "header_within_header",
      "<div data-mobile-role='header'>123<div data-mobile-role='header'>"
      "456</div>789</div>");
  CheckVariable(MobilizeRewriteFilter::kKeeperBlocks, 0);
  CheckVariable(MobilizeRewriteFilter::kHeaderBlocks, 2);
  CheckVariable(MobilizeRewriteFilter::kNavigationalBlocks, 0);
  CheckVariable(MobilizeRewriteFilter::kContentBlocks, 0);
  CheckVariable(MobilizeRewriteFilter::kMarginalBlocks, 0);
  CheckVariable(MobilizeRewriteFilter::kDeletedElements, 0);
}

class MobilizeRewriteThemeTest : public MobilizeRewriteFunctionalTest {
 protected:
  virtual bool LayoutMode() const { return false; }
};

TEST_F(MobilizeRewriteThemeTest, ConfigureTheme) {
  options()->ClearSignatureForTesting();
  ASSERT_EQ(RewriteOptions::kOptionOk,
            options()->SetOptionFromName(RewriteOptions::kMobTheme,
                                         "#ff0000 #0000ff"));
  GoogleString original = StrCat("<head></head>", Body());

  GoogleString expected = StrCat(
      "<head>", HeadAndViewport(false /* layout_mode */), Styles(LayoutMode()),
      "</head>", ExpectedBody("[255,0,0]", "[0,0,255]"));
  ValidateExpected("ConfigureTheme", original, expected);

  ASSERT_EQ(RewriteOptions::kOptionOk,
            options()->SetOptionFromName(RewriteOptions::kMobTheme,
                                         "#ff0000 #0000ff http://logo.com"));
  expected = StrCat("<head>", HeadAndViewport(false /* layout_mode*/),
                    Styles(LayoutMode()), "</head>",
                    ExpectedBody("[255,0,0]", "[0,0,255]"));
  ValidateExpected("ConfigureTheme2", original, expected);
}

TEST_F(MobilizeRewriteThemeTest, PreComputeTheme) {
  options()->ClearSignatureForTesting();
  options()->EnableFilter(RewriteOptions::kMobilizePrecompute);
  GoogleString original = StrCat("<head></head>", Body());

  GoogleString expected =
      StrCat("<head>", HeadAndViewport(false /* layout_mode */),
             Styles(LayoutMode()), "</head>", ExpectedBody());
  ValidateExpected("Precompute", original, expected);
}

// Check we are called correctly from the driver.
class MobilizeRewriteEndToEndTest : public MobilizeRewriteFilterTest {
 protected:
  MobilizeRewriteEndToEndTest() {}

  virtual void SetUp() {
    RewriteTestBase::SetUp();
    SetHtmlMimetype();  // Don't wrap scripts in <![CDATA[ ]]>
    options()->ClearSignatureForTesting();
    options()->set_mob_phone_number(kPhoneNumber);
    options()->set_mob_conversion_id(kConversionId);
    options()->set_mob_phone_conversion_label(kPhoneConversionLabel);
    options()->set_mob_beacon_url(kMobBeaconUrl);
    options()->set_mob_layout(false);
    options()->set_mob_nav(true);
  }

  virtual bool AddBody() const { return false; }
  virtual bool AddHtmlTags() const { return false; }
  virtual bool LayoutMode() const { return options()->mob_layout(); }

  void Layout(bool layout) {
    options()->set_mob_layout(layout);
    AddFilter(RewriteOptions::kMobilize);
  }

  void ValidateWithUA(StringPiece test_name, StringPiece user_agent,
                      StringPiece input, StringPiece expected) {
    SetCurrentUserAgent(user_agent);
    // We need to add the input to our fetcher so the
    // menu extractor can see it.
    GoogleString url = StrCat(kTestDomain, test_name, ".html");
    SetResponseWithDefaultHeaders(url, kContentTypeHtml, input, 1000);
    ValidateExpected(test_name, input, expected);
  }

  GoogleString NoScriptRedirect(StringPiece test_name) const {
    GoogleString url = StrCat(kTestDomain, test_name,
                              ".html?PageSpeed=noscript");
    return StringPrintf(kNoScriptRedirectFormatter, url.c_str(), url.c_str());
  }

  StdioFileSystem filesystem_;

 private:
  DISALLOW_COPY_AND_ASSIGN(MobilizeRewriteEndToEndTest);
};

TEST_F(MobilizeRewriteEndToEndTest, FullPageLayout) {
  // These tests will break when the CSS is changed. Update the expected output
  // accordingly.
  Layout(true);
  GoogleString original_buffer;
  GoogleString original_filename =
      StrCat(GTestSrcDir(), kTestDataDir, kOriginal);
  ASSERT_TRUE(filesystem_.ReadFile(original_filename.c_str(), &original_buffer,
                                   message_handler()));
  GoogleString rewritten_buffer;
  GoogleString rewritten_filename =
      StrCat(GTestSrcDir(), kTestDataDir, kRewritten);
  ASSERT_TRUE(filesystem_.ReadFile(rewritten_filename.c_str(),
                                   &rewritten_buffer, message_handler()));
  GlobalReplaceSubstring("@@VIEWPORT@@", "", &rewritten_buffer);
  GlobalReplaceSubstring(
      "@@SPACER@@",
      StrCat(NoScriptRedirect("EndToEndMobileLayout"), Spacer(), Scrim()),
      &rewritten_buffer);
  GlobalReplaceSubstring("@@HEAD_SCRIPT_LOAD@@",
                         HeadAndViewport(true /* layout_mode */),
                         &rewritten_buffer);
  GlobalReplaceSubstring("@@HEAD_STYLES@@", Styles(true), &rewritten_buffer);
  GlobalReplaceSubstring("@@TRAILING_SCRIPT_LOADS@@", ScriptsAtEndOfBody(),
                         &rewritten_buffer);
  ValidateWithUA("EndToEndMobileLayout",
                 UserAgentMatcherTestBase::kAndroidChrome21UserAgent,
                 original_buffer, rewritten_buffer);
}

TEST_F(MobilizeRewriteEndToEndTest, NonMobileLayout) {
  // Don't mobilize on a non-mobile browser.
  Layout(true);
  GoogleString original_buffer;
  GoogleString original_filename =
      StrCat(GTestSrcDir(), kTestDataDir, kOriginal);
  ASSERT_TRUE(filesystem_.ReadFile(original_filename.c_str(), &original_buffer,
                                   message_handler()));
  ValidateWithUA("EndToEndNonMobileLayout",
                 UserAgentMatcherTestBase::kChrome37UserAgent,
                 original_buffer, original_buffer);
}

TEST_F(MobilizeRewriteEndToEndTest, FullPage) {
  Layout(false);
  GoogleString original_buffer;
  GoogleString original_filename =
      StrCat(GTestSrcDir(), kTestDataDir, kOriginal);
  ASSERT_TRUE(filesystem_.ReadFile(original_filename.c_str(), &original_buffer,
                                   message_handler()));
  GoogleString rewritten_buffer;
  GoogleString rewritten_filename =
      StrCat(GTestSrcDir(), kTestDataDir, kRewritten);
  ASSERT_TRUE(filesystem_.ReadFile(rewritten_filename.c_str(),
                                   &rewritten_buffer, message_handler()));

  GlobalReplaceSubstring("@@VIEWPORT@@",
                         "<meta name=\"viewport\" content=\"width=100px;\"/>",
                         &rewritten_buffer);
  GlobalReplaceSubstring("@@SPACER@@",
                         StrCat(NoScriptRedirect("EndToEndMobile"), Spacer()),
                         &rewritten_buffer);
  GlobalReplaceSubstring("@@HEAD_SCRIPT_LOAD@@",
                         HeadAndViewport(false /* layout_mode */),
                         &rewritten_buffer);
  GlobalReplaceSubstring("@@HEAD_STYLES@@", Styles(false), &rewritten_buffer);
  GlobalReplaceSubstring("@@TRAILING_SCRIPT_LOADS@@", ScriptsAtEndOfBody(),
                         &rewritten_buffer);
  ValidateWithUA("EndToEndMobile",
                 UserAgentMatcherTestBase::kAndroidChrome21UserAgent,
                 original_buffer, rewritten_buffer);
}

TEST_F(MobilizeRewriteEndToEndTest, NonMobile) {
  // Don't mobilize on a non-mobile browser.
  Layout(false);
  GoogleString original_buffer;
  GoogleString original_filename =
      StrCat(GTestSrcDir(), kTestDataDir, kOriginal);
  ASSERT_TRUE(filesystem_.ReadFile(original_filename.c_str(), &original_buffer,
                                   message_handler()));
  ValidateWithUA("EndToEndNonMobile",
                 UserAgentMatcherTestBase::kChrome37UserAgent,
                 original_buffer, original_buffer);
}

class MobilizeRewriteFilterNoLayoutTest : public MobilizeRewriteFunctionalTest {
 protected:
  virtual bool LayoutMode() const { return false; }
};

TEST_F(MobilizeRewriteFilterNoLayoutTest, AddStyleAndViewport) {
  HeadTest("add_style_and_viewport", "", "", 0, 0);
}

TEST_F(MobilizeRewriteFilterNoLayoutTest, BeaconCat) {
  options()->ClearSignatureForTesting();
  options()->set_mob_beacon_category("'experiment2'");
  server_context()->ComputeSignature(options());
  Parse("beacon_cat", "<head>");
  EXPECT_NE(GoogleString::npos, output_buffer_.find(
            ";window.psMobBeaconCategory=\'\\'experiment2\\'\';"))
      << output_buffer_;
}

}  // namespace

}  // namespace net_instaweb
