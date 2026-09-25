# ---- G. 路径名展开（通配） ----
mkdir -p gd && cd gd && : > a.txt && : > b.txt && : > c.log && : > .hidden && cd ..
chk gl_star '2' "$(cd gd && set -- *.txt; echo $#)"
chk gl_star_all '3' "$(cd gd && set -- *; echo $#)"
chk gl_qmark '2' "$(cd gd && set -- ?.txt; echo $#)"
chk gl_bracket '2' "$(cd gd && set -- [ab].txt; echo $#)"
chk gl_bracket_neg '1' "$(cd gd && set -- [!a].txt; echo $#)"
chk gl_bracket_range '3' "$(cd gd && set -- [a-c].*; echo $#)"
chk gl_nomatch_literal 'nosuch*.txt' "$(cd gd && echo nosuch*.txt)"
chk gl_quoted '*.txt' "$(cd gd && echo '*.txt')"
chk gl_dotfiles_excluded '3' "$(cd gd && set -- *; echo $#)"
chk gl_multiple '3' "$(cd gd && set -- *.txt *.log; echo $#)"
chk gl_case_pattern 'match' "$(case ab.txt in *.txt) echo match;; *) echo no;; esac)"
chk gl_glob_in_var 'a.txt' "$(cd gd && p=*.txt; set -- $p; echo "$1")"
