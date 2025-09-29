.data

SendRtpAudioCallback proto C
printf proto C

printf_line db "%p", 10, 0

.code

SendRtpAudioHook proc
push rcx
push rdx
sub rsp, 32
lea rcx, [printf_line]
mov rdx, [rsp+48]
call printf
add rsp, 32
pop rdx
pop rcx
jmp SendRtpAudioCallback
SendRtpAudioHook endp

end

