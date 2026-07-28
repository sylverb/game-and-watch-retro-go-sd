set pagination off
target extended-remote :1234
continue

set $last_idx = log_idx
while 1
    if log_idx != $last_idx
        if log_idx < $last_idx
            printf "%s", &logbuf[0]
        else
            printf "%s", &logbuf[$last_idx]
        end
        set $last_idx = log_idx
    end
    shell sleep 0.1
end

