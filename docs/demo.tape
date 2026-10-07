Output demo.gif
Set Shell "bash"
Set FontSize 18
Set Width 1200
Set Height 620
Set Padding 24
Set TypingSpeed 12ms
Set Framerate 15
Set Theme { "name": "remember", "background": "#111111", "foreground": "#E5E5E5", "cursor": "#F59E0B", "selection": "#3B2A66", "black": "#111111", "red": "#EF4444", "green": "#A3E635", "yellow": "#F59E0B", "blue": "#8B5CF6", "magenta": "#A78BFA", "cyan": "#C4B5FD", "white": "#E5E5E5", "brightBlack": "#6B7280", "brightRed": "#F87171", "brightGreen": "#BEF264", "brightYellow": "#FBBF24", "brightBlue": "#A78BFA", "brightMagenta": "#C4B5FD", "brightCyan": "#DDD6FE", "brightWhite": "#FFFFFF" }

Hide
Type "env -i HOME=/tmp/demo TERM=xterm-256color PATH=/workspace/remember-docs/bin:/usr/bin:/bin REMEMBER_DB=/tmp/demo/remember.db PS1='$ ' bash --norc --noprofile" Enter
Type "rm -rf /tmp/demo/* && mkdir -p /tmp/demo && cd /tmp/demo && clear" Enter
Sleep 300ms
Show

Type `remember add --key workflow:code-review --tag workflow "Review: clean code, SOLID, tests"` Enter
Sleep 1.2s
Type `remember add --key decision:db --tag decision "Use SQLite, one local file"` Enter
Sleep 1.2s
Type "remember search review" Enter
Sleep 2.5s
Type "# the agent side:" Enter
Type "remember --json get --key workflow:code-review | jq '.entries[0] | {key, body, tags, bin}'" Enter
Sleep 3s
Type "clear" Enter
Type `remember add --key status:demo --tag status "Step 1 of 3: schema done"` Enter
Sleep 1s
Type `remember update --key status:demo --text "Step 2 of 3: CLI wired, tests next"` Enter
Sleep 1s
Type "remember get --key status:demo" Enter
Sleep 2.5s
Type "remember list" Enter
Sleep 3s
