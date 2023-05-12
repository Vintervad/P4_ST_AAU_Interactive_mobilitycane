% clc
% close all
% clear
% 
% Fs = 200;
% filename = "InitielTest08maj_05.csv";
% data = readmatrix(filename);
% time_vector = transpose((0:length(data)-1)/Fs);
% acc_data = data(3:end,4);
% step_length_mean = 0.6875;
% distance_walked = 15*step_length_mean;


function [time_walked, walking_speed_kmh, step_counter, full_speed_velocity, velocity_fraction] = regn_ganghastighed_08maj(filename,Fs,step_length_mean,data,distance_walked)

% load(filename)



%%
% Vi trækker 0'er fra vores data, så vi kun har ikke-nul værdier
data_nnz(:,1) = nonzeros(data(:,4));
% data_nnz(:,2) = nonzeros(data(:,2));
% data_nnz(:,3) = nonzeros(data(:,3));


if isempty(data_nnz) == 1
     fprintf("WARNING: Data is empty\n")
     return;
end

% 
%% Data laves om til datatypen 'double'
% raw_x = double(data_nnz(:,1));
raw_y = double(data_nnz(:,1));

% if mean(abs(raw_x))


%% Funktionen mapfun() bruges til at omregne til g
% Altså siger vi at værdien 1565 svarer til -1g og 2399 svarer til 1g
% raw_acc_x = -mapfun(raw_x,1565,2399,-1,1); % Inverteres da x-aksen vendte modsat gangretningen under forsøget 
% raw_acc_x = raw_acc_x-mean(raw_acc_x);

% Altså siger vi at værdien 1561 svarer til -1g og 2397 svarer til 1g
raw_acc_y = mapfun(raw_y,1561,2397,-1,1);
raw_acc_y = raw_acc_y-mean(raw_acc_y);

% Altså siger vi at værdien 1565 svarer til -1g og 2401 svarer til 1g
% raw_acc_z = mapfun(raw_z,1565,2401,-1,1);
% raw_acc_z = raw_acc_z-mean(raw_acc_z);

% Her samles vectorerne til én matrix
raw_acc = [raw_acc_y];


if mean(abs(raw_acc(:,1))) < 0.02
     fprintf("WARNING: Amplitude is too small\n")
     return;
end

time_vector = (0:length(raw_acc)-1)/Fs; %Tidsvektor til plots

% % figure
% % hold on
% % plot(time_vector,raw_acc(:,1))
% % plot(time_vector(nonzeros(locations_maxima)),filtered_signal(nonzeros(locations_maxima)), 'kx')
% % plot(time_vector(nonzeros(locations_minima)),filtered_signal(nonzeros(locations_minima)), 'kx')
% % plot(time_vector(walking_initiated),filtered_signal(walking_initiated), 'ko')
% % plot(time_vector(walking_stopped),filtered_signal(walking_stopped), 'ko')
% % xlim([0 time_vector(end)])
% % xlabel("Tid (s)")
% % ylabel("Δ Acceleration (g)")
% % xlim([0 time_vector(end)])
% % sgtitle("Test af ganghastighedsalgoritme: Ingen gang (rå data fra Y-aksen)")


%% MOVING AVERAGE FILTER
filtered_acc = length(raw_acc);             % Der allokeres plads til filtreret signal og pladsen udfyldes med nuller
filter_length = 0.4*Fs;                    % Filterlængde bestemmes

for axis = 1:1
    for t = filter_length/2:length(raw_acc(:,axis))-filter_length/2
        %filtered_acc(i,axis) = (1/filter_length)*sum(raw_acc(i-filter_length:i,axis));
        t1 = t-filter_length/2+1;
        t2 = t+filter_length/2;
        filtered_acc(t,axis) = mean(raw_acc(t1:t2,axis));
    end 
end

filtered_acc = filtered_acc(2:end,1);

%% Præliminær detektion af peaks
% - Vi bruger funktionen findpeaks() til at finde y-værdien for peaks,
%   x-værdien for peaks og prominence af peaks (højde ift. nærliggende data)
% - Tiden mellem peaks sættes til at være mindst 350ms
% - Der sættes et threshold for den minimale prominence (halvdelen af
%   gennemsnittet af den absolutte værdi af dataen)
% - Der sættes en maksimal og minimal bredde på peaks (125ms - 1200ms)
[maxima, locations_maxima, ~, prominence_maxima] = findpeaks(filtered_acc(filter_length/2:end,1),'MinPeakDistance',0.35*Fs,'MinPeakProminence',mean(abs(filtered_acc(filter_length/2:end,1))/2),'MinPeakWidth',0.125*Fs,'MaxPeakWidth',1.2*Fs,'Annotate','extents');
[minima, locations_minima, ~, prominence_minima] = findpeaks(-filtered_acc(filter_length/2:end,1),'MinPeakDistance',0.35*Fs,'MinPeakProminence',mean(abs(filtered_acc(filter_length/2:end,1))/2),'MinPeakWidth',0.125*Fs,'MaxPeakWidth',1.2*Fs,'Annotate','extents');

%% Plot af findpeaks() for positive peaks
figure
hold on
findpeaks(filtered_acc(10:end,1),'MinPeakDistance',0.35*Fs,'MinPeakProminence',mean(abs(filtered_acc(filter_length/2:end,1))/2),'MinPeakWidth',0.125*Fs,'MaxPeakWidth',1.2*Fs,'Annotate','extents');


%% Her tilpasses detektionen af peaks til det enkelte datasæt
% - Der sættes her samme thresholds for distance mellem peaks, prominence
%   og bredde af peaks
% - Der er her angivet hvor høje peaks må være baseret på halvdelen af
%   højden på den gennemsnitlige peak som vi fandt i den præliminære
%   detektion af peaks
[maxima, locations_maxima, ~, prominence_maxima] = findpeaks(filtered_acc(filter_length/2:end,1),'MinPeakDistance',0.35*Fs,'MinPeakProminence',mean(prominence_maxima)/2.5,'MinPeakWidth',0.125*Fs,'MaxPeakWidth',1.2*Fs,'Annotate','extents');
[minima, locations_minima, ~, prominence_minima] = findpeaks(-filtered_acc(filter_length/2:end,1),'MinPeakDistance',0.35*Fs,'MinPeakProminence',mean(prominence_minima)/2.5,'MinPeakWidth',0.125*Fs,'MaxPeakWidth',1.2*Fs,'Annotate','extents');



%% Her finder vi hvor personen er begyndt at gå
% Denne sektion finder en peak ved det første skridt og går derefter et
% antal samples tilbage for at estimere / forudsige hvor personen er
% begyndt at gå

%Her definerer vi hvor lang vi vil gå tilbage efter den første peak 
window_length_start     = 1.250*Fs; 
window_length_stop      = (window_length_start)-0.25*Fs;

[cpt_locations_start] = findchangepts(filtered_acc(locations_maxima(1)-window_length_start:locations_maxima(1),1),'MaxNumChanges',5,'Statistic','mean');

walking_initiated = locations_maxima(1) - window_length_start + cpt_locations_start(1);

% 
% figure
% hold on
% findpeaks(-abs(filtered_acc(locations_maxima(1)-1000:locations_maxima(1),1)),'MinPeakProminence',mean(prominence_maxima)/10)

%% Her finder vi hvor personen er stoppet med at gå
% Denne sektion finder en peak ved det sidste skridt og går derefter et
% antal samples frem for at estimere / forudsige hvor personen er stoppet
% med at gå

% Her fortæller vi algoritmen at hvis der ikke er nok samples efter den
% sidste peak til at kunne lave et vindue på den specificerede
% vinduesstørrelse, skal vindet være samme størrelse som de resterende data
if locations_minima(end) + window_length_stop > length(filtered_acc)
    window_length_stop = length(filtered_acc) - locations_minima(end);
% else 
%     window_length_stop = window_length_stop;
end


[cpt_locations_stop] = findchangepts(filtered_acc(locations_minima(end):locations_minima(end)+window_length_stop,1),'MaxNumChanges',5,'Statistic','mean');

walking_stopped = locations_minima(end) + cpt_locations_stop(end);

% figure
% hold on
% findchangepts(filtered_acc(locations_minima(end):locations_minima(end)+window_length_stop,1),'MaxNumChanges',5,'Statistic','mean')


%% 
filtered_signal = filtered_acc(filter_length/2:end,1); % Her navngives data noget andet til plots

time_vector = (0:length(filtered_signal)-1)/Fs; %Tidsvektor til plots

%% Plot af signalet med peaks markeret
% Her plottes data med detektion af peaks samt start og stop
% figure
% hold on
% plot(filtered_signal)
% plot(nonzeros(locations_maxima),filtered_signal(nonzeros(locations_maxima)), 'bx')
% plot(nonzeros(locations_minima),filtered_signal(nonzeros(locations_minima)), 'kx')
% plot(walking_initiated,filtered_signal(walking_initiated), 'ko')
% plot(walking_stopped,filtered_signal(walking_stopped), 'ko')
% xlim([0 length(filtered_signal)])


%% Regn ganghastighed
%Her regnes ganghastigheden baseret på hvornår man er startet og stoppet
%med at gå

% distance_walked bliver loaded ind sammen med dataen --> det er tilpasset
% til hver optagelse
time_walked = (walking_stopped-walking_initiated)/Fs; %Antal sekunder mellem første og sidste skridt

walking_speed_ms = distance_walked/time_walked; % m/s

walking_speed_kmh = walking_speed_ms * 3.6; % km/t

%%


%% Tid mellem skridt

% 
%     for i = 1:min(length(locations_maxima),length(locations_minima))
%        %time_between_steps(i) = round((locations_minima(i) - locations_maxima(i))/2);
%        steps_actual(i) = locations_minima(i)
% 
%     end

%Gennemsnitlig skridtlængde i meter
% step_length_mean = distance_walked/length(locations_minima);

steps_full_speed = locations_maxima(1:end);
time_full_speed = (locations_maxima(end) - locations_maxima(1))/Fs;

distance_walked_full_speed = step_length_mean * length(steps_full_speed);

full_speed_velocity = (distance_walked_full_speed/time_full_speed)*3.6;

velocity_fraction = full_speed_velocity/walking_speed_kmh;

%%
figure
hold on
plot(time_vector,filtered_signal)
plot(time_vector(nonzeros(locations_maxima)),filtered_signal(nonzeros(locations_maxima)), 'kx')
plot(time_vector(nonzeros(locations_minima)),filtered_signal(nonzeros(locations_minima)), 'kx')
plot(time_vector(walking_initiated),filtered_signal(walking_initiated), 'ko')
plot(time_vector(walking_stopped),filtered_signal(walking_stopped), 'ko')
xlim([0 time_vector(end)])
xlabel("Tid (s)")
ylabel("Δ Acceleration (g)")



%steps_actual = locations_maxima(1) + time_between_steps

%% WARNINGS

% Advarer hvis der ikke er lige mange troughs og peaks
if length(maxima) ~= length(minima)
    fprintf(" WARNING: Maxima og Minima er ikke lige lange \n")
end

% Advarer hvis antallet af både troughs og peaks er over eller under 15
if length(maxima) > 15 && length(minima) > 15
    fprintf("WARNING: Både maxima og minima er længere end 15 \n")
elseif length(maxima) < 15 && length(minima) < 15
    fprintf("WARNING: Både maxima og minima er kortere end 15 \n")
end


peak_afstand = diff(locations_maxima);

% Advarer hvis der er for langt mellem to peaks
for i = 1:length(peak_afstand)
    if peak_afstand(i) > 1000
        fprintf("WARNING: Peaks afstand for stor\n")
    end
end    


%% Her gør jeg egentlig bare vektorerne lige lange for at kunne gøre ting med dem senere

% Hvis der er flere maxima end minima, tilføjer vi et 0 i enden af vektorerne
% med x- og y-værdier for negative peaks (minima)
if length(maxima) > length(minima)
    minima = [minima;zeros(1,1)];
    locations_minima = [locations_minima;zeros(1,1)];
    
% Hvis der er flere minima end maxima, tilføjer vi et 0 i enden af vektorerne
% med x- og y-værdier for positive peaks (maxima)
elseif length(maxima) < length(minima)
        maxima = [maxima;zeros(1,1)];
        locations_maxima = [locations_maxima;zeros(1,1)];
end

%% Estimering af skridtlængde
% Vi finder her længden mellem positive peaks og negative peaks og tager
% gennemsnittet af disse --> giver måske et mere præcist estimat af hvor
% langt der er mellem peaks
for i = 2:length(maxima)-1
    steps_duration_maxima(i-1) = locations_maxima(i) - locations_maxima(i-1);
    steps_duration_minima(i-1) = locations_minima(i) - locations_minima(i-1);
    steps_duration_average(i-1) =  (steps_duration_maxima(i-1) + steps_duration_minima(i-1))/2;
    
end

steps_duration_average;

step_counter = length(maxima);


%%

% stride_length = (steps_duration_average / sum(steps_duration_average))*time_walked*100;
% 
% stride_length_mean = mean(stride_length);

%distance_walked*100

% figure
% hold on
% subplot(3,1,person)
% plot(steps_duration_average)
% ylabel("Time between steps (ms)")
% xlabel("Step number")


%%
end
























