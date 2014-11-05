/* -*- mode: C++; c-basic-offset: 2; indent-tabs-mode: nil -*- */

/*
 *  Main authors:
 *     Guido Tack <guido.tack@monash.edu>
 */

/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include <minizinc/prettyprinter.hh>
#include <minizinc/htmlprinter.hh>

#include <minizinc/model.hh>
#include <minizinc/eval_par.hh>

#include <sstream>

namespace MiniZinc {

  namespace HtmlDocOutput {
    
    class DocItem {
    public:
      enum DocType { T_PAR=0, T_VAR=1, T_FUN=2 };
      DocItem(const DocType& t0, std::string id0, std::string doc0) : t(t0), id(id0), doc(doc0) {}
      DocType t;
      std::string id;
      std::string doc;
    };
    
    class Group {
    public:
      Group(const std::string& name0) : name(name0) {}
      std::string name;
      std::vector<DocItem> items;
      std::string toHTML(void) {
        struct SortById {
          bool operator ()(const DocItem& i0, const DocItem& i1) {
            return i0.t < i1.t || (i0.t==i1.t && i0.id < i1.id);
          }
        } _cmp;
        std::stable_sort(items.begin(), items.end(), _cmp);
        std::ostringstream oss;
        int cur_t = -1;
        const char* dt[] = {"par","var","fun"};
        const char* dt_desc[] = {"Parameters","Variables","Functions and Predicates"};
        for (std::vector<DocItem>::const_iterator it = items.begin(); it != items.end(); ++it) {
          if (it->t != cur_t) {
            if (cur_t != -1)
              oss << "</div>\n";
            cur_t = it->t;
            oss << "<div class='mzn-decl-type" << dt[cur_t] << ">";
            oss << "<div class='mzn-decl-type-heading'>" << dt_desc[cur_t] << "</div>\n";
          }
          oss << it->doc;
        }
        if (cur_t != -1)
          oss << "</div>\n";
        return oss.str();
      }
    };

    typedef UNORDERED_NAMESPACE::unordered_map<std::string, Group> GroupMap;
    
    void addToGroup(GroupMap& gm, const std::string& group, DocItem& di) {
      if (gm.find(group) == gm.end()) {
        gm.insert(std::make_pair(group,Group(group)));
      }
      gm.find(group)->second.items.push_back(di);
    }
    
  }
  
  class PrintHtmlVisitor : public ItemVisitor {
  protected:
    HtmlDocOutput::GroupMap& _gm;
    
    std::string extractArgWord(std::string& s, size_t n) {
      size_t start = n;
      while (start < s.size() && s[start]!=' ' && s[start]!='\t')
        start++;
      while (start < s.size() && (s[start]==' ' || s[start]=='\t'))
        start++;
      int end = start+1;
      while (end < s.size() && (isalnum(s[end]) || s[end]=='_'))
        end++;
      std::string ret = s.substr(start,end-start);
      s = s.substr(0,n)+s.substr(end,std::string::npos);
      return ret;
    }
    std::pair<std::string,std::string> extractArgLine(std::string& s, size_t n) {
      size_t start = n;
      while (start < s.size() && s[start]!=' ' && s[start]!='\t')
        start++;
      while (start < s.size() && (s[start]==' ' || s[start]=='\t'))
        start++;
      int end = start+1;
      while (end < s.size() && s[end]!=':')
        end++;
      std::string arg = s.substr(start,end-start);
      size_t doc_start = end+1;
      while (end < s.size() && s[end]!='\n')
        end++;
      std::string ret = s.substr(doc_start,end-doc_start);
      s = s.substr(0,n)+s.substr(end,std::string::npos);
      return make_pair(arg,ret);
    }
    
    std::vector<std::string> replaceArgs(std::string& s) {
      std::vector<std::string> replacements;
      std::ostringstream oss;
      size_t lastpos = 0;
      size_t pos = s.find("\\a");
      if (pos == std::string::npos)
        return replacements;
      while (pos != std::string::npos) {
        oss << s.substr(lastpos, pos);
        size_t start = pos;
        while (start < s.size() && s[start]!=' ' && s[start]!='\t')
          start++;
        while (start < s.size() && (s[start]==' ' || s[start]=='\t'))
          start++;
        int end = start+1;
        while (end < s.size() && (isalnum(s[end]) || s[end]=='_'))
          end++;
        replacements.push_back(s.substr(start,end-start));
        oss << "<span class='mzn-arg'>" << replacements.back() << "</span>";
        lastpos = end;
        pos = s.find("\\a", lastpos);
      }
      oss << s.substr(lastpos, std::string::npos);
      s = oss.str();
      return replacements;
    }
    
    void replaceEOLs(const std::string& s, std::ostream& os) {
      size_t lastpos = 0;
      size_t pos = s.find('\n');
      std::cerr << "found pos " << pos << "\n";
      while (pos != std::string::npos) {
        std::cerr << "  found pos " << pos << "\n";
        os << s.substr(lastpos, pos-lastpos);
        os << "<br>";
        lastpos = pos+1;
        pos = s.find('\n', lastpos);
      }
      os << s.substr(lastpos, std::string::npos);
    }
    
  public:
    PrintHtmlVisitor(HtmlDocOutput::GroupMap& gm) : _gm(gm) {}
    /// Visit variable declaration
    void vVarDeclI(VarDeclI* vdi) {
      if (Call* docstring = Expression::dyn_cast<Call>(getAnnotation(vdi->e()->ann(), constants().ann.doc_comment))) {
        
        std::string ds = eval_string(docstring->args()[0]);
        std::string group("main");
        size_t group_idx = ds.find("@group");
        if (group_idx!=std::string::npos) {
          group = extractArgWord(ds, group_idx);
        }
        
        std::ostringstream os;
        os << "<div class='mzn-vardecl'>\n";
        os << "<div class='mzn-vardecl-code'>\n";
        os << *vdi->e()->ti() << ": " << *vdi->e()->id();
        os << "</div><div class='mzn-vardecl-doc'>\n";
        os << ds;
        os << "</div>";
        GCLock lock;
        HtmlDocOutput::DocItem di(vdi->e()->type().ispar() ? HtmlDocOutput::DocItem::T_PAR: HtmlDocOutput::DocItem::T_VAR,
                                  vdi->e()->type().toString()+" "+vdi->e()->id()->str().str(), os.str());
        HtmlDocOutput::addToGroup(_gm, group, di);
      }
    }
    /// Visit function item
    void vFunctionI(FunctionI* fi) {
      if (Call* docstring = Expression::dyn_cast<Call>(getAnnotation(fi->ann(), constants().ann.doc_comment))) {
        std::string ds = eval_string(docstring->args()[0]);
        std::string group("main");
        size_t group_idx = ds.find("@group");
        if (group_idx!=std::string::npos) {
          group = extractArgWord(ds, group_idx);
        }

        size_t param_idx = ds.find("@param");
        std::vector<std::pair<std::string,std::string> > params;
        while (param_idx != std::string::npos) {
          params.push_back(extractArgLine(ds, param_idx));
          param_idx = ds.find("@param");
        }
        
        std::vector<std::string> args = replaceArgs(ds);
        
        UNORDERED_NAMESPACE::unordered_set<std::string> allArgs;
        for (unsigned int i=0; i<args.size(); i++)
          allArgs.insert(args[i]);
        for (unsigned int i=0; i<params.size(); i++)
          allArgs.insert(params[i].first);
        
        GCLock lock;
        for (unsigned int i=0; i<fi->params().size(); i++) {
          if (allArgs.find(fi->params()[i]->id()->str().str()) == allArgs.end()) {
            std::cerr << "Warning: parameter " << *fi->params()[i]->id() << " not documented for function " << fi->id() << "\n";
          }
        }
        
        std::ostringstream os;
        os << "<div class='mzn-fundecl'>\n";
        os << "<div class='mzn-fundecl-code'>\n";
        
        Expression* doc_comment_fn_body = fi->e() ? getAnnotation(fi->ann(), "doc_comment_fn_body") : NULL;
        if (doc_comment_fn_body) {
          fi->ann().remove(docstring);
          fi->ann().remove(doc_comment_fn_body);
          Printer pp(os, 80);
          pp.print(fi);
          fi->ann().add(docstring);
          fi->ann().add(doc_comment_fn_body);
        } else {
          if (fi->ti()->type() == Type::varbool()) {
            os << "predicate ";
          } else if (fi->ti()->type() == Type::parbool()) {
            os << "test ";
          } else {
            os << "function " << *fi->ti() << ": ";
          }
          os << fi->id() << "(";
          for (unsigned int i=0; i<fi->params().size(); i++) {
            os << *fi->params()[i]->ti() << ": " << *fi->params()[i]->id();
            if (i < fi->params().size()-1)
              os << ", ";
          }
          os << ")";
        }
        os << "</div><div class='mzn-fundecl-doc'>\n";
        os << ds;
        if (params.size() > 0) {
          os << "<div class='mzn-fundecl-params-heading'>Parameters</div>\n";
          os << "<ul class='mzn-fundecl-params'>\n";
          for (unsigned int i=0; i<params.size(); i++) {
            os << "<li>" << params[i].first << ": " << params[i].second << "</li>\n";
          }
          os << "</ul>\n";
        }
        os << "</div>";
        os << "</div>";

        HtmlDocOutput::DocItem di(HtmlDocOutput::DocItem::T_FUN, fi->id().str(), os.str());
        HtmlDocOutput::addToGroup(_gm, group, di);
      }
    }
  };
  
  std::vector<HtmlDocument>
  HtmlPrinter::printHtml(MiniZinc::Model* m) {
    using namespace HtmlDocOutput;
    GroupMap gm;
    PrintHtmlVisitor phv(gm);
    iterItems(phv, m);
    std::vector<HtmlDocument> ret;
    for (GroupMap::iterator it = gm.begin(); it != gm.end(); ++it) {
      ret.push_back(HtmlDocument(it->second.name, it->second.toHTML()));
    }
    return ret;
  }
  
  HtmlDocument
  HtmlPrinter::printHtmlSinglePage(MiniZinc::Model* m) {
    using namespace HtmlDocOutput;
    GroupMap gm;
    PrintHtmlVisitor phv(gm);
    iterItems(phv, m);
    Group masterGroup("master");
    for (GroupMap::iterator it = gm.begin(); it != gm.end(); ++it) {
      for (std::vector<DocItem>::iterator git = it->second.items.begin(); git != it->second.items.end(); ++git) {
        masterGroup.items.push_back(*git);
      }
    }
    return HtmlDocument("model.html", masterGroup.toHTML());
  }
  
}