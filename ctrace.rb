#!/usr/bin/ruby -w

is_first = true
tail = nil
input = nil
extra = [
    "typedef int YIP;\n",
    "typedef int STATE;\n",
    "typedef int MACHINE;\n",
    "typedef int YIP_CODE;\n",
    "typedef int YIP_TOKEN;\n",
    "typedef int YIP_SOURCE;\n",
    "typedef int TRANSITION;\n",
    "typedef int YIP_ENCODING;\n",
    "typedef int YIP_PRODUCTION;\n",
    "typedef int DYNAMIC_SOURCE;\n",
    "typedef int FP_READ_SOURCE;\n",
    "typedef int FD_READ_SOURCE;\n",
    "typedef int FD_MMAP_SOURCE;\n",
    "typedef int MACHINE_BY_NAME;\n",
]
replace = [
    [ ' \b',  '' ],
    [ /"\\n[ ]*[0-9]+/,  '__LINE__, "' ],
    [ 't_ct_("',  't_ct_(-1, "' ],
    [ 't_ct_(text)',  "t_ct_(line, text)\nregister int line;" ],
    [ 'PF_CT_"%s", text);', 'if (line >= 0) printf("\n%4d ", line); PF_CT_"%s", text);' ],
    [ 'char *_ct;t_ct_(__LINE__, " ', 'char *_ct;t_ct_(__LINE__, " [[[ ' ],
    [ /"[^"]*return [^"]*"/, '\\0 " ]]]"' ],
]

$stdin.each_line do |line|
    if input
        input.write(line)
        next unless line.include?("/* }}} */")
        input.flush
        input.close
        input = nil
        system("ssh zerver ctrace < junk.input > junk.output") || abort("ctrace: $?")
        seen_start = false
        seen_end = false
        to_ignore = false;
        IO.readlines("junk.output").each do |line|
            next if extra.include?(line);
            next if line.include?('#line')
            to_ignore = true if line.include?("int	(*sigbus)(), (*sigsegv)();")
            if line.include?("signal(SIGSEGV, (void (*)())sigsegv);")
                to_ignore = false
                next
            end
            next if to_ignore;
            line.gsub!(/char\s+\*/, "const char *") if !seen_start || seen_end
            replace.each do |from, into|
                line.gsub!(from, into)
            end
            if seen_end
                line = "\tva_start(plist, _size);\n" if line == "\tva_start(plist, char*);\n"
                tail.push(line)
                next
            end
            if line.include?("/* {{{ */")
                seen_start = true
                if (is_first)
                    print("static t_ct_(int line, const char *text);\n")
                    print("static s_ct_(const char *name, const char *value);\n");
                end
            end
            if line.include?("/* }}} */")
                break if tail
                seen_end = true
                tail = [ line ]
            end
            print line if is_first || seen_start
        end
        is_first = false
    elsif line.include?("/* {{{ */")
        input = File.new("junk.input", "w")
        input.write(extra.join)
        input.write(line)
    else
        print(line)
    end
end
exit(0) if ARGV[0] == 'notail'
tail.each do |line|
    print line
end
