clc
close all
clear

Fs = 200;


subjects = [1:1];
repetitions = [5:5];
average_step_length = [0.6875];


%%

% % Kør ganghastighedsalgoritme for alle forsøgspersoner
for i = 1:length(subjects)
    current_subject = subjects(i);
    person = subjects(i);

    for j = 1:length(repetitions)
        current_repetition  = repetitions(j);
        rep = repetitions(j);

        filename = sprintf("InitielTest08maj_0%d.csv", current_repetition);
        data = readmatrix(filename)
        time_vector = transpose((0:length(data)-1)/Fs);
        acc_data = -data(3:end,4);
        step_length_mean = average_step_length(current_subject);
        distance_walked = 15*step_length_mean;

        eval(sprintf("[~,~,~, full_speed_velocity_subject%d_rep%d] = regn_ganghastighed_08maj(filename,Fs,step_length_mean,data,distance_walked)",current_subject, current_repetition))
        fprintf("Ny forsøgsperson\n")
        

    end
%  close all
end

% Ganghastighed koverteres til m/s

for j = 1:length(repetitions)
    current_repetition  = repetitions(j);
    
    eval(sprintf("v_gang = (full_speed_velocity_subject1_rep%d)*1000/60/60",current_repetition));
    X_F = (1.0336*v_gang+0.1073*v_gang*v_gang+0.5)*100;
    
    eval(sprintf("Alarmeringsafstand_cm_rep%d = round(X_F+0.5)",current_repetition));
       
end

eval(sprintf("alarmeringsafstand_gennemsnit = round(0.5 + (Alarmeringsafstand_cm_rep%d + Alarmeringsafstand_cm_rep%d + Alarmeringsafstand_cm_rep%d)/3)",repetitions(1),repetitions(2),repetitions(3)));





% 
% v_gang = full_speed_velocity_subject1_rep1*1000/60/60
% 
% % X_F regnes ud i cm vha. kalibreringsformlen 
% % X_F = ((0.6836)*v_gang+0.1073*v_gang*v_gang+0.5)*100
% 
% X_F = (1.0336*v_gang+0.1073*v_gang*v_gang+0.5)*100;
% 
% % X_F rundes OP til nærmeste heltal for at få "alarmeringsafstanden"
% Alarmeringsafstand_cm = round(X_F+0.5)
