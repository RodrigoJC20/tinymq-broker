-- public.clients definition

-- Drop table

-- DROP TABLE public.clients;

CREATE TABLE public.clients (
	id serial4 NOT NULL,
	client_id varchar(255) NOT NULL,
	last_connected timestamptz DEFAULT CURRENT_TIMESTAMP NULL,
	last_ip varchar(45) NULL,
	last_port int4 NULL,
	connection_count int4 DEFAULT 1 NULL,
	active bool DEFAULT true NULL,
	CONSTRAINT clients_client_id_key UNIQUE (client_id),
	CONSTRAINT clients_pkey PRIMARY KEY (id)
);


-- public.connection_events definition

-- Drop table

-- DROP TABLE public.connection_events;

CREATE TABLE public.connection_events (
	id serial4 NOT NULL,
	client_id varchar(255) NOT NULL,
	event_type varchar(20) NOT NULL,
	ip_address varchar(45) NULL,
	port int4 NULL,
	"timestamp" timestamptz DEFAULT CURRENT_TIMESTAMP NULL,
	CONSTRAINT connection_events_pkey PRIMARY KEY (id),
	CONSTRAINT connection_events_client_id_fkey FOREIGN KEY (client_id) REFERENCES public.clients(client_id)
);
CREATE INDEX idx_connection_events_client ON public.connection_events USING btree (client_id);


-- public.topics definition

-- Drop table

-- DROP TABLE public.topics;

CREATE TABLE public.topics (
	id serial4 NOT NULL,
	"name" varchar(255) NOT NULL,
	owner_client_id varchar(255) NOT NULL,
	created_at timestamptz DEFAULT CURRENT_TIMESTAMP NULL,
	publish bool DEFAULT false NULL,
	CONSTRAINT topics_name_key UNIQUE (name),
	CONSTRAINT topics_pkey PRIMARY KEY (id),
	CONSTRAINT topics_owner_client_id_fkey FOREIGN KEY (owner_client_id) REFERENCES public.clients(client_id)
);
CREATE INDEX idx_topics_owner ON public.topics USING btree (owner_client_id);


-- public.admin_requests definition

-- Drop table

-- DROP TABLE public.admin_requests;

CREATE TABLE public.admin_requests (
	id serial4 NOT NULL,
	topic_id int4 NULL,
	requester_client_id text NULL,
	status text NULL,
	request_timestamp timestamp DEFAULT CURRENT_TIMESTAMP NULL,
	response_timestamp timestamp NULL,
	CONSTRAINT admin_requests_pkey PRIMARY KEY (id),
	CONSTRAINT admin_requests_status_check CHECK ((status = ANY (ARRAY['pending'::text, 'approved'::text, 'rejected'::text, 'revoked'::text]))),
	CONSTRAINT admin_requests_topic_id_requester_client_id_key UNIQUE (topic_id, requester_client_id),
	CONSTRAINT admin_requests_requester_client_id_fkey FOREIGN KEY (requester_client_id) REFERENCES public.clients(client_id),
	CONSTRAINT admin_requests_topic_id_fkey FOREIGN KEY (topic_id) REFERENCES public.topics(id)
);


-- public.admin_sensor_config definition

-- Drop table

-- DROP TABLE public.admin_sensor_config;

CREATE TABLE public.admin_sensor_config (
	topic_id int4 NOT NULL,
	sensor_name text NOT NULL,
	active bool DEFAULT true NULL,
	set_by text NULL,
	updated_at timestamp DEFAULT CURRENT_TIMESTAMP NULL,
	activable bool DEFAULT false NULL,
	CONSTRAINT admin_sensor_config_pkey PRIMARY KEY (topic_id, sensor_name),
	CONSTRAINT admin_sensor_config_set_by_fkey FOREIGN KEY (set_by) REFERENCES public.clients(client_id),
	CONSTRAINT admin_sensor_config_topic_id_fkey FOREIGN KEY (topic_id) REFERENCES public.topics(id)
);


-- public.message_logs definition

-- Drop table

-- DROP TABLE public.message_logs;

CREATE TABLE public.message_logs (
	id serial4 NOT NULL,
	publisher_client_id varchar(255) NOT NULL,
	topic_id int4 NOT NULL,
	payload_size int4 NOT NULL,
	payload_preview varchar(500) NULL,
	published_at timestamptz DEFAULT CURRENT_TIMESTAMP NULL,
	CONSTRAINT message_logs_pkey PRIMARY KEY (id),
	CONSTRAINT message_logs_publisher_client_id_fkey FOREIGN KEY (publisher_client_id) REFERENCES public.clients(client_id),
	CONSTRAINT message_logs_topic_id_fkey FOREIGN KEY (topic_id) REFERENCES public.topics(id)
);
CREATE INDEX idx_message_logs_publisher ON public.message_logs USING btree (publisher_client_id);
CREATE INDEX idx_message_logs_topic ON public.message_logs USING btree (topic_id);


-- public.subscriptions definition

-- Drop table

-- DROP TABLE public.subscriptions;

CREATE TABLE public.subscriptions (
	id serial4 NOT NULL,
	client_id varchar(255) NOT NULL,
	topic_id int4 NOT NULL,
	subscribed_at timestamptz DEFAULT CURRENT_TIMESTAMP NULL,
	active bool DEFAULT true NULL,
	CONSTRAINT subscriptions_client_id_topic_id_key UNIQUE (client_id, topic_id),
	CONSTRAINT subscriptions_pkey PRIMARY KEY (id),
	CONSTRAINT subscriptions_client_id_fkey FOREIGN KEY (client_id) REFERENCES public.clients(client_id),
	CONSTRAINT subscriptions_topic_id_fkey FOREIGN KEY (topic_id) REFERENCES public.topics(id)
);
CREATE INDEX idx_subscriptions_client ON public.subscriptions USING btree (client_id);
CREATE INDEX idx_subscriptions_topic ON public.subscriptions USING btree (topic_id);


-- public.topic_admins definition

-- Drop table

-- DROP TABLE public.topic_admins;

CREATE TABLE public.topic_admins (
	topic_id int4 NOT NULL,
	admin_client_id text NOT NULL,
	granted_at timestamp DEFAULT CURRENT_TIMESTAMP NULL,
	CONSTRAINT topic_admins_pkey PRIMARY KEY (topic_id, admin_client_id),
	CONSTRAINT topic_admins_admin_client_id_fkey FOREIGN KEY (admin_client_id) REFERENCES public.clients(client_id),
	CONSTRAINT topic_admins_topic_id_fkey FOREIGN KEY (topic_id) REFERENCES public.topics(id)
);
CREATE INDEX idx_topic_admins_admin ON public.topic_admins USING btree (admin_client_id);