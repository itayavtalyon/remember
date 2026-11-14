# SARIF in, one line per alert out: "BLOCK|allowed<TAB>rule<TAB>path:line<TAB>message".
# $allow is scripts/codeql-allow.tsv. The entries that apply to an alert are those
# with its rule and a prefix of its path. Each input source the message names,
# "[text](id)", is matched as "<source file>: <text>", the file taken from the
# relatedLocations entry with that id ("?" when there is none). The alert passes
# when EVERY source contains the text of one of those entries (or, when it names
# none, the message contains one). So each source of a multi-source alert needs its
# own reviewed entry, and an entry that starts with a file allows that source only
# when it comes from that file.
($allow | split("\n") | map(select(length > 0 and (startswith("#") | not)) | split("\t"))) as $rows
| .runs[].results[]
| (.locations[0].physicalLocation | "\(.artifactLocation.uri):\(.region.startLine)") as $loc
| .ruleId as $rule | .message.text as $msg
| (.relatedLocations // []) as $rel
| ([$msg | scan("\\[([^\\]]*)\\](?:\\((\\d+)\\))?")
    | . as [$text, $id]
    | ([$rel[] | select($id != null and .id == ($id | tonumber))][0].physicalLocation.artifactLocation.uri // "?")
    | "\(.): \($text)"]) as $srcs
| [$rows[] | select(. as $r | $r[0] == $rule and ($loc | startswith($r[1])))] as $m
| (if ($srcs | length) == 0 then any($m[]; . as $r | $msg | contains($r[2]))
   else all($srcs[]; . as $s | any($m[]; . as $r | $s | contains($r[2]))) end) as $ok
| [(if $ok then "allowed" else "BLOCK" end), $rule, $loc, ($msg | gsub("\n"; " "))]
| @tsv
